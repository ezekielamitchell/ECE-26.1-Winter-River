# Contributing to ECE-26.1-Winter-River

Thank you for your interest in contributing to this IoT environmental monitoring system! This document provides guidelines for contributing to the project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Process](#development-process)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Pull Request Process](#pull-request-process)
- [Reporting Bugs](#reporting-bugs)
- [Feature Requests](#feature-requests)

## Code of Conduct

This project follows a code of conduct that ensures a welcoming environment for all contributors. Please be respectful, constructive, and professional in all interactions.

### Expected Behavior

- Use welcoming and inclusive language
- Be respectful of differing viewpoints and experiences
- Accept constructive criticism gracefully
- Focus on what is best for the community and project
- Show empathy towards other community members

### Unacceptable Behavior

- Harassment, trolling, or insulting comments
- Publishing others' private information without permission
- Any other conduct that could be considered inappropriate in a professional setting

## Getting Started

### Prerequisites

1. Install development tools:
   - Python 3.9 or higher
   - PlatformIO CLI or VS Code with PlatformIO extension
   - Git
   - Docker and Docker Compose

2. Fork the repository on GitHub

3. Clone your fork:

```bash
git clone https://github.com/YOUR_USERNAME/ECE-26.1-Winter-River.git
cd ECE-26.1-Winter-River
```

4. Add the upstream repository:

```bash
git remote add upstream https://github.com/ORIGINAL_OWNER/ECE-26.1-Winter-River.git
```

### Setting Up Development Environment

#### Python Broker Development

```bash
cd broker
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
pip install -r requirements.txt
pip install -r requirements-dev.txt  # Development dependencies
```

#### ESP32 Development

```bash
cd esp/node1
pio run  # Build firmware
pio test  # Run tests
```

## Development Process

### Branching Strategy

- `main` - Production-ready code
- `develop` - Integration branch for features
- `feature/*` - New features
- `bugfix/*` - Bug fixes
- `hotfix/*` - Critical production fixes

### Workflow

1. Create a new branch from `develop`:

```bash
git checkout develop
git pull upstream develop
git checkout -b feature/your-feature-name
```

2. Make your changes following the coding standards

3. Write or update tests as needed

4. Run tests locally:

```bash
# Python tests
cd broker
pytest tests/

# ESP32 tests
cd esp/node1
pio test
```

5. Commit your changes with clear messages:

```bash
git add .
git commit -m "Add feature: description of your feature"
```

6. Push to your fork:

```bash
git push origin feature/your-feature-name
```

7. Create a Pull Request on GitHub

## Coding Standards

### Python (Broker)

- Follow PEP 8 style guide
- Use type hints for function parameters and return values
- Maximum line length: 100 characters
- Use descriptive variable and function names
- Add docstrings to all public functions and classes

Example:

```python
def process_sensor_data(data: dict, node_id: str) -> bool:
    """
    Process incoming sensor data from an ESP32 node.

    Args:
        data: Dictionary containing sensor readings
        node_id: Unique identifier for the sensor node

    Returns:
        True if processing succeeded, False otherwise
    """
    # Implementation here
    pass
```

### C++ (ESP32)

- Follow ESP-IDF style guidelines
- Use meaningful variable names
- Comment complex logic
- Use const and constexpr where appropriate
- Avoid global variables when possible

Example:

```cpp
/**
 * @brief Read temperature from DHT22 sensor
 *
 * @return float Temperature in Celsius, or -999.0 on error
 */
float readTemperature() {
    // Implementation here
    return 0.0;
}
```

### General Guidelines

- Keep functions small and focused (single responsibility)
- Write self-documenting code with clear names
- Add comments for complex algorithms or non-obvious logic
- Avoid premature optimization
- Handle errors gracefully with appropriate logging

## Testing

### Python Tests

Run the full test suite:

```bash
cd broker
pytest tests/ -v
```

Run with coverage:

```bash
pytest tests/ --cov=src --cov-report=html
```

### ESP32 Tests

```bash
cd esp/node1
pio test -v
```

### Integration Tests

```bash
# Start test environment
docker-compose -f docker-compose.test.yml up -d

# Run integration tests
pytest tests/integration/

# Cleanup
docker-compose -f docker-compose.test.yml down
```

## Pull Request Process

1. **Update Documentation**: Ensure README and other docs reflect your changes

2. **Test Thoroughly**: All tests must pass before submitting

3. **Update CHANGELOG**: Add a summary of your changes

4. **Fill PR Template**: Provide clear description of changes and motivation

5. **Request Review**: Tag relevant maintainers for review

6. **Address Feedback**: Respond to review comments and make necessary changes

7. **Squash Commits**: Before merge, squash commits into logical units

### PR Title Format

```text
[Type] Brief description

Types:
- [Feature] - New functionality
- [Bugfix] - Bug fix
- [Docs] - Documentation changes
- [Refactor] - Code restructuring without behavior change
- [Test] - Test additions or modifications
- [CI] - CI/CD pipeline changes
```

### PR Description Template

```markdown
## Description
Brief description of changes

## Motivation
Why this change is needed

## Changes Made
- Change 1
- Change 2
- Change 3

## Testing
How you tested these changes

## Screenshots (if applicable)
Add screenshots for UI changes

## Checklist
- [ ] Tests pass locally
- [ ] Documentation updated
- [ ] CHANGELOG updated
- [ ] Code follows style guidelines
- [ ] Commits are descriptive
```

## Reporting Bugs

### Before Submitting

1. Check existing issues to avoid duplicates
2. Test with the latest version
3. Gather relevant information (logs, environment details)

### Bug Report Template

```markdown
**Description**
Clear description of the bug

**To Reproduce**
Steps to reproduce:
1. Go to '...'
2. Click on '...'
3. See error

**Expected Behavior**
What should happen

**Actual Behavior**
What actually happens

**Environment**
- OS: [e.g., Raspberry Pi OS Bullseye]
- Python Version: [e.g., 3.9.2]
- ESP-IDF Version: [e.g., 4.4.2]

**Logs**
```text
Paste relevant logs here
```

**Additional Context**
Any other relevant information
```

## Feature Requests

We welcome feature ideas! Please:

1. Check if the feature already exists or is planned
2. Clearly describe the feature and its benefits
3. Provide use cases and examples
4. Be open to discussion and alternative approaches

### Feature Request Template

```markdown
**Feature Description**
Clear description of the proposed feature

**Use Case**
Why this feature would be useful

**Proposed Implementation**
Ideas for how this could be implemented

**Alternatives Considered**
Other approaches you've thought about

**Additional Context**
Any other relevant information
```

## Questions?

If you have questions about contributing, feel free to:

- Open a GitHub Discussion
- Ask in existing issues
- Contact the maintainers

Thank you for contributing to ECE-26.1-Winter-River!
```
