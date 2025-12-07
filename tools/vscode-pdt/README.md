# Perfect Dark Tooling (PDT) VS Code Extension

This extension provides VS Code commands to interact with the Perfect Dark build tools (`pdt`).

## Features

- **PDT: Build Port**: Runs `pdt build-port` to build the PC port.
- **PDT: Clean and Build Port**: Runs `pdt build-port --clean`.
- **PDT: Run ROM Tool**: Runs `pdt rom` with custom arguments.

## Configuration

- `pdt.path`: Path to the `pdt` script. Defaults to `${workspaceFolder}/../tools/docker-caroll/bin/pdt`.

## Development

1. Open this folder in VS Code.
2. Run `npm install`.
3. Press F5 to launch the extension in a new Extension Development Host window.
