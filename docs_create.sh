#!/bin/bash

# Script to build and run Doxygen documentation generation using Docker
# Usage: ./docs_create.sh [--clean]
# Options:
#   --clean    Remove existing docs directory before generating new documentation
#   --docbook-to FORMAT  Convert DocBook to additional formats:
#                       epub,pdf,html,txt (comma-separated)
#   -h, --help           Show this help message


set -e  # Exit on any error

# Parse command line arguments
CLEAN_BUILD=false
DOCBOOK_FORMATS=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_BUILD=true
            shift
            ;;
        --docbook-to)
            DOCBOOK_FORMATS="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [--clean] [--docbook-to FORMAT]"
            echo "Options:"
            echo "  --clean              Remove existing docs directory before generating"
            echo "  --docbook-to FORMAT  Convert DocBook to additional formats:"
            echo "                       epub,pdf,html,txt (comma-separated)"
            echo "  -h, --help           Show this help message"
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
IMAGE_NAME="doc-doxygen"
CONTAINER_NAME="doc-doxygen-run"
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
        if [ -d "${DOCS_OUTPUT_DIR}" ]; then
            docker run --rm -v "$(pwd)/docs:/docs" alpine:latest chown -R "$(id -u):$(id -g)" /docs
            rm -rf "${DOCS_OUTPUT_DIR}"
        fi
    fi
    
    print_status "Creating output directory: ${DOCS_OUTPUT_DIR}"
    mkdir -p "${DOCS_OUTPUT_DIR}"
}

# Function to run Doxygen and generate PDF
run_doxygen() {
    print_status "Running Doxygen documentation generation..."
    
    # Always copy the latest Doxyfile to ensure we use the updated configuration
    print_status "Copying latest Doxyfile configuration..."
    cp docker/Doxyfile "${DOCS_OUTPUT_DIR}/"
    
    # Copy custom header and footer for Doxygen Awesome theme
    print_status "Copying custom HTML header and footer..."
    cp docker/header.html "${DOCS_OUTPUT_DIR}/"
    cp docker/footer.html "${DOCS_OUTPUT_DIR}/"
    
    # Copy custom LaTeX header for Unicode support
    print_status "Copying custom LaTeX header for Unicode support..."
    cp docker/latex_header.tex "${DOCS_OUTPUT_DIR}/"
    
    # Run the Docker container
    if ! docker run --rm \
        --name "${CONTAINER_NAME}" \
        -v "$(pwd):/docs/project:ro" \
        -v "${DOCS_OUTPUT_DIR}:/docs/output" \
        -w /docs/output \
        "${IMAGE_NAME}" \
        bash -c "
        cp -r /docs/doxygen-awesome . && 
        # First generate HTML, RTF, DocBook with all files (including Unicode markdown files)
        doxygen Doxyfile && 
        # Then generate LaTeX separately with excluded Unicode markdown files for PDF generation
        echo "Creating LaTeX-specific configuration excluding Unicode markdown files..."
        sed -e 's|EXCLUDE                    = .*\.orig _.*|EXCLUDE                    = *.orig _* /docs/project/docs_artic /docs/project/README.md|' \
            -e 's|GENERATE_HTML            = YES|GENERATE_HTML            = NO|' \
            -e 's|GENERATE_RTF             = YES|GENERATE_RTF             = NO|' \
            -e 's|GENERATE_DOCBOOK         = YES|GENERATE_DOCBOOK         = NO|' \
            /docs/project/docker/Doxyfile > Doxyfile.latex
        echo 'Generating LaTeX documentation without Unicode markdown files...' &&
        doxygen Doxyfile.latex &&
        if [ -d latex ]; then 
            cd latex && 
            echo 'Generating PDF documentation...' &&
            make 2>&1 | tee latex_build.log || {
                echo 'PDF generation failed, check latex_build.log for details'
                echo 'HTML documentation is still available'
                exit 0
            }
        fi &&
        # Fix ownership of generated files
        chown -R $(id -u):$(id -g) /docs/output 2>/dev/null || true"; then
        print_error "Failed to generate documentation"
        exit 1
    fi
    
    print_success "Documentation generated successfully"
    print_status "Documentation available at:"
    print_status "  HTML: ${DOCS_OUTPUT_DIR}/html/index.html"
    [ -f "${DOCS_OUTPUT_DIR}/latex/refman.pdf" ] && print_status "  PDF:  ${DOCS_OUTPUT_DIR}/latex/refman.pdf"
    [ -d "${DOCS_OUTPUT_DIR}/rtf" ] && print_status "  RTF:  ${DOCS_OUTPUT_DIR}/rtf/"
    [ -d "${DOCS_OUTPUT_DIR}/docbook" ] && print_status "  DocBook: ${DOCS_OUTPUT_DIR}/docbook/"
}

# Function to process DocBook to additional formats
process_docbook() {
    if [ -z "$DOCBOOK_FORMATS" ] || [ ! -d "${DOCS_OUTPUT_DIR}/docbook" ]; then
        return 0
    fi
    
    print_status "Processing DocBook to additional formats..."
    
    # Install processing tools in a temporary container
    docker run --rm \
        -v "${DOCS_OUTPUT_DIR}:/docs/output" \
        -w /docs/output \
        ubuntu:22.04 \
        bash -c "
        apt-get update >/dev/null 2>&1 && 
        apt-get install -y xmlto pandoc fop >/dev/null 2>&1 &&
        cd docbook &&
        
        IFS=',' read -ra FORMATS <<< '$DOCBOOK_FORMATS'
        for fmt in \"\${FORMATS[@]}\"; do
            case \$fmt in
                epub)
                    echo 'Converting to EPUB...'
                    if ls *.xml >/dev/null 2>&1; then
                        pandoc \$(ls *.xml | head -1) -f docbook -t epub -o ../documentation.epub 2>/dev/null || 
                        xmlto epub \$(ls *.xml | head -1) -o .. 2>/dev/null || echo 'EPUB conversion failed'
                    fi
                    ;;
                pdf)
                    echo 'Converting to PDF via DocBook...'
                    if ls *.xml >/dev/null 2>&1; then
                        xmlto pdf \$(ls *.xml | head -1) -o .. 2>/dev/null || echo 'DocBook PDF conversion failed'
                    fi
                    ;;
                html)
                    echo 'Converting to single HTML...'
                    if ls *.xml >/dev/null 2>&1; then
                        xmlto html-nochunks \$(ls *.xml | head -1) -o .. 2>/dev/null || echo 'HTML conversion failed'
                    fi
                    ;;
                txt)
                    echo 'Converting to plain text...'
                    if ls *.xml >/dev/null 2>&1; then
                        pandoc \$(ls *.xml | head -1) -f docbook -t plain -o ../documentation.txt 2>/dev/null || echo 'Text conversion failed'
                    fi
                    ;;
                *)
                    echo \"Unknown format: \$fmt\"
                    ;;
            esac
        done
        "
    
    print_status "DocBook conversions completed. Check docs/ directory for additional formats."
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
    print_status "Starting Doxygen documentation generation"
    
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
    process_docbook
    
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
