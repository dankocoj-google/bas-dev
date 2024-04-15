# Use Ubuntu as the base image (choose a specific version if needed)
FROM ubuntu:latest

# Update the package lists to ensure you have the latest versions
RUN apt-get update

# Install essential build tools for some potential gcloud component dependencies
RUN apt-get install -y build-essential git curl


# Install Docker (using the convenience script from Docker's repo)
RUN curl -fsSL https://get.docker.com -o get-docker.sh \
    && sh get-docker.sh

# Install Google Cloud SDK 
RUN echo "deb [signed-by=/usr/share/keyrings/cloud.google.gpg] https://packages.cloud.google.com/apt cloud-sdk main" | tee -a /etc/apt/sources.list.d/google-cloud-sdk.list \
    && curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | apt-key --keyring /usr/share/keyrings/cloud.google.gpg add - \
    && apt-get update && apt-get install -y google-cloud-sdk

# Set up a working directory
WORKDIR /app

# Best Practice: Clear build cache after package installs
RUN apt-get clean && rm -rf /var/lib/apt/lists/*