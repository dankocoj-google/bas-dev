# Use a debian-based image with gcloud and docker installed:
FROM google/cloud-sdk:473.0.0

# Update the package lists to ensure you get the latest packages
RUN apt-get update

# Install additional dependencies
RUN apt-get install -y \
    git \
    gettext-base  # Package for envsubst