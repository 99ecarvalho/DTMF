# Docker Documentation Setup

### Copyright (c) 2025
### Author: Eduardo Correia <ecorreia@apliant.com.br>

This directory contains Docker configuration for generating project documentation using Doxygen with the modern Doxygen Awesome theme.

## Files

- `Dockerfile.doxygen`: Docker image definition for Doxygen documentation generation with Doxygen Awesome theme
- `Doxyfile`: Doxygen configuration file optimized for the project with modern styling
- `header.html`: Custom HTML header with dark mode toggle support
- `footer.html`: Custom HTML footer with modern styling
- `../run_doxygen.sh`: Bash script to build and run the documentation generation

## Features

The documentation generation includes:

- **Modern UI**: Uses the Doxygen Awesome theme for a clean, modern interface
- **Dark Mode**: Toggle between light and dark themes
- **Interactive Elements**: Copy buttons, paragraph links, and enhanced navigation
- **Enhanced Graphs**: SVG-based class diagrams with UML styling and transparency
- **Search Functionality**: Built-in search engine for quick navigation
- **Responsive Design**: Mobile-friendly layout that adapts to different screen sizes
- **Call/Caller Graphs**: Visual representation of function dependencies

## Requirements

- Docker installed and running
- Bash shell (for the runner script)

## Usage

### Quick Start

From the project root directory, run:

```bash
./run_doxygen.sh
```

This script will:
1. Build the Docker image with Doxygen and required tools
2. Create a `docs` directory in the project root
3. Generate HTML documentation from your C++ source code
4. Optionally open the documentation in your browser

### Manual Docker Commands

If you prefer to run Docker commands manually:

```bash
# Build the image
docker build -t doc-doxygen -f docker/Dockerfile.doxygen docker/

# Create output directory
mkdir -p docs

# Copy configuration
cp docker/Doxyfile docs/

# Run documentation generation
docker run --rm \
  -v "$(pwd):/docs/project:ro" \
  -v "$(pwd)/docs:/docs/output" \
  -w /docs/output \
  doc-doxygen \
  doxygen Doxyfile
```

## Output

The generated documentation will be available in:
- `docs/html/index.html` - Main HTML documentation page
- `docs/html/` - Complete HTML documentation

## Configuration

The `Doxyfile` is configured with modern features including:

- Include all C++ source files (`.cpp`, `.h`, `.hpp`)
- Generate HTML output with Doxygen Awesome theme styling
- Interactive SVG graphs and UML-style class diagrams
- Dark mode support with toggle functionality
- Enhanced search engine for quick navigation
- Call graphs and collaboration diagrams
- Exclude build directories and Python cache files
- Extract all documented and undocumented code
- Modern color scheme optimized for readability

You can modify the `docker/Doxyfile` to customize the documentation generation according to your needs.

## Troubleshooting

1. **Docker not found**: Ensure Docker is installed and in your PATH
2. **Permission denied**: Make sure `run_doxygen.sh` is executable (`chmod +x run_doxygen.sh`)
3. **Build fails**: Check that you're running the script from the project root directory
4. **No documentation**: Ensure your C++ files contain Doxygen comments

## Doxygen Comment Examples

To get the best documentation, add Doxygen comments to your C++ code:

```cpp
/**
 * @brief Brief description of the class
 * 
 * Detailed description of what this class does.
 */
class MyClass {
public:
    /**
     * @brief Brief description of the method
     * @param param1 Description of first parameter
     * @param param2 Description of second parameter
     * @return Description of return value
     */
    int myMethod(int param1, double param2);
    
private:
    int m_member; ///< Description of member variable
};
```
