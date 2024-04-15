//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <aws/core/Aws.h>
#include <google/protobuf/util/json_util.h>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/ext/proto_server_reflection_plugin.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "services/auction_service/auction_code_fetch_config.pb.h"
#include "services/auction_service/auction_service.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/data/runtime_config.h"
#include "services/auction_service/runtime_flags.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/code_fetch/periodic_code_fetcher.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/telemetry/configure_telemetry.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/public/cpio/interface/cpio.h"
#include "src/util/rlimit_core_config.h"
#include "src/util/status_macro/status_macros.h"

ABSL_FLAG(std::optional<uint16_t>, port, std::nullopt,
          "Port the server is listening on.");
ABSL_FLAG(std::optional<uint16_t>, healthcheck_port, std::nullopt,
          "Non-TLS port dedicated to healthchecks. Must differ from --port.");
ABSL_FLAG(
    bool, init_config_client, false,
    "Initialize config client to fetch any runtime flags not supplied from"
    " command line from cloud metadata store. False by default.");
ABSL_FLAG(std::optional<bool>, enable_auction_service_benchmark, std::nullopt,
          "Benchmark the auction server and write the runtimes to the logs.");
ABSL_FLAG(
    std::optional<std::string>, seller_code_fetch_config, std::nullopt,
    "The JSON string for config fields necessary for AdTech code fetching.");
ABSL_FLAG(
    std::optional<std::int64_t>, js_num_workers, std::nullopt,
    "The number of workers/threads for executing AdTech code in parallel.");
ABSL_FLAG(std::optional<std::int64_t>, js_worker_queue_len, std::nullopt,
          "The length of queue size for a single JS execution worker.");
ABSL_FLAG(std::optional<bool>, enable_report_win_input_noising, std::nullopt,
          "Enables noising and bucketing of the inputs to reportWin");

namespace privacy_sandbox::bidding_auction_servers {

using ::google::scp::cpio::Cpio;
using ::google::scp::cpio::CpioOptions;
using ::google::scp::cpio::LogOption;
using ::grpc::Server;
using ::grpc::ServerBuilder;

absl::StatusOr<TrustedServersConfigClient> GetConfigClient(
    absl::string_view config_param_prefix) {
  TrustedServersConfigClient config_client(GetServiceFlags());
  config_client.SetFlag(FLAGS_port, PORT);
  config_client.SetFlag(FLAGS_healthcheck_port, HEALTHCHECK_PORT);
  config_client.SetFlag(FLAGS_enable_auction_service_benchmark,
                        ENABLE_AUCTION_SERVICE_BENCHMARK);
  config_client.SetFlag(FLAGS_test_mode, TEST_MODE);
  config_client.SetFlag(FLAGS_roma_timeout_ms, ROMA_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_public_key_endpoint, PUBLIC_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_primary_coordinator_private_key_endpoint,
                        PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_secondary_coordinator_private_key_endpoint,
                        SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_primary_coordinator_account_identity,
                        PRIMARY_COORDINATOR_ACCOUNT_IDENTITY);
  config_client.SetFlag(FLAGS_secondary_coordinator_account_identity,
                        SECONDARY_COORDINATOR_ACCOUNT_IDENTITY);
  config_client.SetFlag(FLAGS_primary_coordinator_region,
                        PRIMARY_COORDINATOR_REGION);
  config_client.SetFlag(FLAGS_secondary_coordinator_region,
                        SECONDARY_COORDINATOR_REGION);
  config_client.SetFlag(FLAGS_gcp_primary_workload_identity_pool_provider,
                        GCP_PRIMARY_WORKLOAD_IDENTITY_POOL_PROVIDER);
  config_client.SetFlag(FLAGS_gcp_secondary_workload_identity_pool_provider,
                        GCP_SECONDARY_WORKLOAD_IDENTITY_POOL_PROVIDER);
  config_client.SetFlag(FLAGS_gcp_primary_key_service_cloud_function_url,
                        GCP_PRIMARY_KEY_SERVICE_CLOUD_FUNCTION_URL);
  config_client.SetFlag(FLAGS_gcp_secondary_key_service_cloud_function_url,
                        GCP_SECONDARY_KEY_SERVICE_CLOUD_FUNCTION_URL);

  config_client.SetFlag(FLAGS_private_key_cache_ttl_seconds,
                        PRIVATE_KEY_CACHE_TTL_SECONDS);
  config_client.SetFlag(FLAGS_key_refresh_flow_run_frequency_seconds,
                        KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS);
  config_client.SetFlag(FLAGS_telemetry_config, TELEMETRY_CONFIG);
  config_client.SetFlag(FLAGS_seller_code_fetch_config,
                        SELLER_CODE_FETCH_CONFIG);
  config_client.SetFlag(FLAGS_js_num_workers, JS_NUM_WORKERS);
  config_client.SetFlag(FLAGS_js_worker_queue_len, JS_WORKER_QUEUE_LEN);
  config_client.SetFlag(FLAGS_enable_report_win_input_noising,
                        ENABLE_REPORT_WIN_INPUT_NOISING);
  config_client.SetFlag(FLAGS_consented_debug_token, CONSENTED_DEBUG_TOKEN);
  config_client.SetFlag(FLAGS_enable_otel_based_logging,
                        ENABLE_OTEL_BASED_LOGGING);
  config_client.SetFlag(FLAGS_enable_protected_app_signals,
                        ENABLE_PROTECTED_APP_SIGNALS);
  config_client.SetFlag(FLAGS_ps_verbosity, PS_VERBOSITY);
  config_client.SetFlag(FLAGS_max_allowed_size_debug_url_bytes,
                        MAX_ALLOWED_SIZE_DEBUG_URL_BYTES);
  config_client.SetFlag(FLAGS_max_allowed_size_all_debug_urls_kb,
                        MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB);

  if (absl::GetFlag(FLAGS_init_config_client)) {
    PS_RETURN_IF_ERROR(config_client.Init(config_param_prefix)).LogError()
        << "Config client failed to initialize.";
  }
  // Set verbosity
  server_common::log::PS_VLOG_IS_ON(
      0, config_client.GetIntParameter(PS_VERBOSITY));

  PS_VLOG(1) << "Protected App Signals support enabled on the service: "
             << config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  PS_VLOG(1) << "Successfully constructed the config client.";
  return config_client;
}

inline void CollectBuyerOriginEndpoints(
    const google::protobuf::Map<std::string, std::string>&
        buyer_reporting_code_url,
    std::vector<std::string>& buyer_origins,
    std::vector<std::string>& endpoints) {
  for (const auto& [buyer_origin, report_win_url] : buyer_reporting_code_url) {
    buyer_origins.push_back(buyer_origin);
    endpoints.push_back(report_win_url);
  }
}

// Gets code blob mapping from buyer origin => code blob fetched using the code
// fetcher. Note: Caller needs to ensure that the `buyer_origins` and
// `ad_tech_code_blobs` are in sync with each other and the `first` and `last`
// indices are in bound.
absl::flat_hash_map<std::string, std::string> GetCodeBlobMap(
    int first, int last, const std::vector<std::string>& buyer_origins,
    const std::vector<std::string>& ad_tech_code_blobs,
    absl::string_view data_type) {
  absl::flat_hash_map<std::string, std::string> code_blob_map;
  for (unsigned int ind = first; ind < last; ind++) {
    if (auto [unused_it, inserted] = code_blob_map.try_emplace(
            buyer_origins[ind], ad_tech_code_blobs[ind]);
        !inserted) {
      PS_VLOG(0) << "Malformed config for '" << data_type
                 << "': Possible duplicate entries for buyer: "
                 << buyer_origins[ind]
                 << ", existing map size: " << code_blob_map.size();
    }
  }
  return code_blob_map;
}

// Brings up the gRPC AuctionService on FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));

  std::string_view port = config_client.GetStringParameter(PORT);
  std::string server_address = absl::StrCat("0.0.0.0:", port);

  CHECK(!config_client.GetStringParameter(SELLER_CODE_FETCH_CONFIG).empty())
      << "SELLER_CODE_FETCH_CONFIG is a mandatory flag.";

  auto dispatcher = V8Dispatcher([&config_client]() {
    DispatchConfig config;
    config.worker_queue_max_items =
        config_client.GetIntParameter(JS_WORKER_QUEUE_LEN);
    config.number_of_workers = config_client.GetIntParameter(JS_NUM_WORKERS);
    return config;
  }());
  CodeDispatchClient client(dispatcher);

  PS_RETURN_IF_ERROR(dispatcher.Init()) << "Could not start code dispatcher.";

  server_common::GrpcInit gprc_init;
  std::unique_ptr<server_common::Executor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          grpc_event_engine::experimental::CreateEventEngine());
  std::unique_ptr<HttpFetcherAsync> http_fetcher =
      std::make_unique<MultiCurlHttpFetcherAsync>(executor.get());

  std::unique_ptr<CodeFetcherInterface> code_fetcher;

  // Convert Json string into a AuctionCodeBlobFetcherConfig proto
  auction_service::SellerCodeFetchConfig code_fetch_proto;
  absl::Status result = google::protobuf::util::JsonStringToMessage(
      config_client.GetStringParameter(SELLER_CODE_FETCH_CONFIG).data(),
      &code_fetch_proto);
  CHECK(result.ok()) << "Could not parse SELLER_CODE_FETCH_CONFIG JsonString "
                        "to a proto message: "
                     << result;

  bool enable_seller_debug_url_generation =
      code_fetch_proto.enable_seller_debug_url_generation();
  bool enable_adtech_code_logging =
      code_fetch_proto.enable_adtech_code_logging();
  bool enable_report_result_url_generation =
      code_fetch_proto.enable_report_result_url_generation();
  bool enable_report_win_url_generation =
      code_fetch_proto.enable_report_win_url_generation();
  std::vector<std::string> endpoints;
  std::vector<std::string> buyer_origins;
  CollectBuyerOriginEndpoints(code_fetch_proto.buyer_report_win_js_urls(),
                              buyer_origins, endpoints);
  const int num_protected_audience_endpoints = buyer_origins.size();
  const bool enable_protected_app_signals =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  if (enable_protected_app_signals) {
    CollectBuyerOriginEndpoints(
        code_fetch_proto.protected_app_signals_buyer_report_win_js_urls(),
        buyer_origins, endpoints);
  }

  // Starts periodic code blob fetching from an arbitrary url only if js_url is
  // specified
  std::string js_url = code_fetch_proto.auction_js_url();
  if (!js_url.empty()) {
    endpoints.push_back(js_url);
    auto wrap_code =
        [num_protected_audience_endpoints, enable_protected_app_signals,
         enable_report_result_url_generation, enable_report_win_url_generation,
         buyer_origins](const std::vector<std::string>& ad_tech_code_blobs) {
          CHECK(buyer_origins.size() == ad_tech_code_blobs.size() - 1)
              << "Error fetching code blobs from buyer. Buyer size:"
              << buyer_origins.size()
              << " and blobs count:" << ad_tech_code_blobs.size();
          // Collect code blobs for protected audience.
          auto buyer_origin_code_map = GetCodeBlobMap(
              /*first=*/0, num_protected_audience_endpoints, buyer_origins,
              ad_tech_code_blobs, "buyer_report_win_js_urls");
          // Collect code blobs for protected app signals.
          absl::flat_hash_map<std::string, std::string>
              protected_app_signals_buyer_origin_code_map;
          if (enable_protected_app_signals) {
            protected_app_signals_buyer_origin_code_map = GetCodeBlobMap(
                num_protected_audience_endpoints, buyer_origins.size(),
                buyer_origins, ad_tech_code_blobs,
                "protected_app_signals_buyer_report_win_js_urls");
          }
          return GetSellerWrappedCode(
              ad_tech_code_blobs.at(ad_tech_code_blobs.size() - 1),
              enable_report_result_url_generation, enable_protected_app_signals,
              enable_report_win_url_generation, buyer_origin_code_map,
              protected_app_signals_buyer_origin_code_map);
        };

    code_fetcher = std::make_unique<PeriodicCodeFetcher>(
        endpoints, absl::Milliseconds(code_fetch_proto.url_fetch_period_ms()),
        http_fetcher.get(), &dispatcher, executor.get(),
        absl::Milliseconds(code_fetch_proto.url_fetch_timeout_ms()),
        std::move(wrap_code), "v1");

    code_fetcher->Start();
  } else if (!code_fetch_proto.auction_js_path().empty()) {
    std::ifstream ifs(code_fetch_proto.auction_js_path().data());
    std::string adtech_code_blob((std::istreambuf_iterator<char>(ifs)),
                                 (std::istreambuf_iterator<char>()));

    adtech_code_blob = GetSellerWrappedCode(
        adtech_code_blob, enable_report_result_url_generation, false, {});

    PS_RETURN_IF_ERROR(dispatcher.LoadSync("v1", adtech_code_blob))
        << "Could not load Adtech untrusted code for scoring.";
  } else {
    return absl::UnavailableError(
        "Code fetching config requires either a path or url.");
  }

  bool enable_auction_service_benchmark =
      config_client.GetBooleanParameter(ENABLE_AUCTION_SERVICE_BENCHMARK);

  InitTelemetry<ScoreAdsRequest>(config_util, config_client, metric::kAs);

  std::unique_ptr<AsyncReporter> async_reporter =
      std::make_unique<AsyncReporter>(
          std::make_unique<MultiCurlHttpFetcherAsync>(executor.get()));
  auto score_ads_reactor_factory =
      [&client, &async_reporter, enable_auction_service_benchmark](
          const ScoreAdsRequest* request, ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger;
        if (enable_auction_service_benchmark) {
          benchmarkingLogger = std::make_unique<ScoreAdsBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarkingLogger = std::make_unique<ScoreAdsNoOpLogger>();
        }
        return std::make_unique<ScoreAdsReactor>(
            client, request, response, std::move(benchmarkingLogger),
            key_fetcher_manager, crypto_client, async_reporter.get(),
            runtime_config);
      };

  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_seller_debug_url_generation,
      .roma_timeout_ms =
          config_client.GetStringParameter(ROMA_TIMEOUT_MS).data(),
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_win_url_generation,
      .enable_protected_app_signals =
          config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS),
      .enable_report_win_input_noising =
          config_client.GetBooleanParameter(ENABLE_REPORT_WIN_INPUT_NOISING),
      .max_allowed_size_debug_url_bytes =
          config_client.GetIntParameter(MAX_ALLOWED_SIZE_DEBUG_URL_BYTES),
      .max_allowed_size_all_debug_urls_kb =
          config_client.GetIntParameter(MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB)};
  AuctionService auction_service(
      std::move(score_ads_reactor_factory),
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr),
      CreateCryptoClient(), std::move(runtime_config));

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  // This server is expected to accept insecure connections as it will be
  // deployed behind an HTTPS load balancer that terminates TLS.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

  if (config_client.HasParameter(HEALTHCHECK_PORT)) {
    CHECK(config_client.GetStringParameter(HEALTHCHECK_PORT) !=
          config_client.GetStringParameter(PORT))
        << "Healthcheck port must be unique.";
    builder.AddListeningPort(
        absl::StrCat("0.0.0.0:",
                     config_client.GetStringParameter(HEALTHCHECK_PORT)),
        grpc::InsecureServerCredentials());
  }

  // Set max message size to 256 MB.
  builder.AddChannelArgument(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
                             256L * 1024L * 1024L);
  builder.RegisterService(&auction_service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    return absl::UnavailableError("Error starting Server.");
  }
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  PS_VLOG(1) << "Server listening on " << server_address;
  server->Wait();
  // Ends periodic code blob fetching from an arbitrary url.
  if (code_fetcher) {
    code_fetcher->End();
  }
  return absl::OkStatus();
}
}  // namespace privacy_sandbox::bidding_auction_servers

int main(int argc, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  privacysandbox::server_common::SetRLimits({
      .enable_core_dumps = PS_ENABLE_CORE_DUMPS,
  });
  absl::FailureSignalHandlerOptions options;
  absl::InstallFailureSignalHandler(options);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  google::scp::cpio::CpioOptions cpio_options;

  bool init_config_client = absl::GetFlag(FLAGS_init_config_client);
  if (init_config_client) {
    cpio_options.log_option = google::scp::cpio::LogOption::kConsoleLog;
    CHECK(google::scp::cpio::Cpio::InitCpio(cpio_options).Successful())
        << "Failed to initialize CPIO library";
  }

  CHECK_OK(privacy_sandbox::bidding_auction_servers::RunServer())
      << "Failed to run server.";

  if (init_config_client) {
    google::scp::cpio::Cpio::ShutdownCpio(cpio_options);
  }

  return 0;
}
