# Use a debian-based image with gcloud and docker installed:
FROM gcr.io/google.com/cloudsdktool/cloud-sdk:latest

# Update the package lists to ensure you get the latest packages
RUN apt-get update

# Install additional dependencies
RUN apt-get install -y \
    git \
    gettext-base  # Package for envsubst