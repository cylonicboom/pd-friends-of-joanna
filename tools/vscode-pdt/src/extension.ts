import * as vscode from 'vscode';
import * as path from 'path';

export function activate(context: vscode.ExtensionContext) {
    console.log('PDT extension is now active!');

    context.subscriptions.push(
        vscode.commands.registerCommand('pdt.buildPort', () => {
            runPdtCommand('build-port');
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('pdt.cleanBuild', () => {
            runPdtCommand('build-port', ['--clean']);
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('pdt.rom', async () => {
            const args = await vscode.window.showInputBox({
                prompt: 'Enter arguments for pdt rom command'
            });
            if (args !== undefined) {
                runPdtCommand('rom', args.split(' '));
            }
        })
    );
}

function getPdtPath(): string {
    const config = vscode.workspace.getConfiguration('pdt');
    let pdtPath = config.get<string>('path');
    
    if (!pdtPath) {
        // Fallback default relative to workspace
        if (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length > 0) {
             const wsPath = vscode.workspace.workspaceFolders[0].uri.fsPath;
             // Default assumption based on repo structure
             pdtPath = path.join(wsPath, '../tools/docker-caroll/bin/pdt');
        } else {
            return 'pdt'; // Assume in PATH
        }
    }

    // Resolve ${workspaceFolder}
    if (pdtPath && pdtPath.includes('${workspaceFolder}') && vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length > 0) {
        pdtPath = pdtPath.replace('${workspaceFolder}', vscode.workspace.workspaceFolders[0].uri.fsPath);
    }

    return pdtPath || 'pdt';
}

function runPdtCommand(command: string, args: string[] = []) {
    const pdtPath = getPdtPath();
    const terminal = vscode.window.createTerminal(`PDT: ${command}`);
    terminal.show();
    
    // Quote path if it contains spaces
    const quotedPath = pdtPath.includes(' ') ? `"${pdtPath}"` : pdtPath;
    
    terminal.sendText(`${quotedPath} ${command} ${args.join(' ')}`);
}

export function deactivate() {}
