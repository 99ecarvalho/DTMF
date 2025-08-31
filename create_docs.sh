#!/bin/bash

# Script to build and run Doxygen documentation generation using Docker
# Usage: ./run_doxygen.sh [--clean]
# Options:
#   --clean    Remove existing docs directory before generating new documentation

set -e  # Exit on any error

# Parse command line arguments
CLEAN_BUILD=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--clean]"
            echo "Options:"
            echo "  --clean    Remove existing docs directory before generating"
            echo "  -h, --help Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
IMAGE_NAME="tke-fusion-doxygen"
CONTAINER_NAME="tke-doxygen-run"
DOCS_OUTPUT_DIR="$(pwd)/docs"

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to cleanup previous containers
cleanup() {
    print_status "Cleaning up previous containers..."
    if docker ps -a --format 'table {{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
        docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true
    fi
}

# Function to build the Docker image
build_image() {
    print_status "Building Doxygen Docker image..."
    if ! docker build -t "${IMAGE_NAME}" -f docker/Dockerfile.doxygen docker/; then
        print_error "Failed to build Docker image"
        exit 1
    fi
    print_success "Docker image built successfully"
}

# Function to create output directory
create_output_dir() {
    # Clean existing docs if requested
    if [ "$CLEAN_BUILD" = true ]; then
        print_status "Cleaning existing documentation directory..."
        rm -rf "${DOCS_OUTPUT_DIR}"
    fi
    
    print_status "Creating output directory: ${DOCS_OUTPUT_DIR}"
    mkdir -p "${DOCS_OUTPUT_DIR}"
}

# Function to run Doxygen
run_doxygen() {
    print_status "Running Doxygen documentation generation..."
    
    # Always copy the latest Doxyfile to ensure we use the updated configuration
    print_status "Copying latest Doxyfile configuration..."
    cp docker/Doxyfile "${DOCS_OUTPUT_DIR}/"
    
    # Copy custom header and footer for Doxygen Awesome theme
    print_status "Copying custom HTML header and footer..."
    cp docker/header.html "${DOCS_OUTPUT_DIR}/"
    cp docker/footer.html "${DOCS_OUTPUT_DIR}/"
    
    # Run the Docker container
    if ! docker run --rm \
        --name "${CONTAINER_NAME}" \
        -v "$(pwd):/docs/project:ro" \
        -v "${DOCS_OUTPUT_DIR}:/docs/output" \
        -w /docs/output \
        "${IMAGE_NAME}" \
        bash -c "cp -r /docs/doxygen-awesome . && doxygen Doxyfile"; then
        print_error "Failed to generate documentation"
        exit 1
    fi
    
    print_success "Documentation generated successfully"
    print_status "Documentation available at: ${DOCS_OUTPUT_DIR}/html/index.html"
}

# Function to open documentation in browser (optional)
open_docs() {
    local html_file="${DOCS_OUTPUT_DIR}/html/index.html"
    if [ -f "${html_file}" ]; then
        print_status "Opening documentation in browser..."
        if command -v xdg-open >/dev/null 2>&1; then
            xdg-open "${html_file}" >/dev/null 2>&1 &
        elif command -v open >/dev/null 2>&1; then
            open "${html_file}" >/dev/null 2>&1 &
        else
            print_warning "Could not open browser automatically"
            print_status "Please open: file://${html_file}"
        fi
    fi
}

# Main execution
main() {
    print_status "Starting Doxygen documentation generation for TKE Fusion project"
    
    # Check if Docker is available
    if ! command -v docker >/dev/null 2>&1; then
        print_error "Docker is not installed or not available in PATH"
        exit 1
    fi
    
    # Check if Docker daemon is running
    if ! docker info >/dev/null 2>&1; then
        print_error "Docker daemon is not running"
        exit 1
    fi
    
    # Change to script directory
    cd "$(dirname "$0")"
    
    # Execute steps
    cleanup
    build_image
    create_output_dir
    run_doxygen
    
    # Ask if user wants to open documentation
    read -p "Would you like to open the documentation in your browser? (y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        open_docs
    fi
    
    print_success "Doxygen documentation generation completed!"
}

# Handle script interruption
trap 'print_error "Script interrupted"; cleanup; exit 1' INT TERM

# Run main function
main "$@"
