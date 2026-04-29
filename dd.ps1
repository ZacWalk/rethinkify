# Rethinkify CLI Tool
# Usage: .\dd.ps1 <command> [options]
#
# Commands:
#   run     - Run the local development server and open in browser
#   deploy  - Deploy to Google App Engine

param(
    [Parameter(Position=0)]
    [ValidateSet("deploy", "run", "format", "help")]
    [string]$Command = "help",

    # Deploy options
    [string]$Project = "rethinkify",
    [string]$Region = "europe-west1",
    [switch]$NoPromote,
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$ScriptRoot = $PSScriptRoot

function Show-Help {
    Write-Host ""
    Write-Host "Rethinkify CLI Tool" -ForegroundColor Cyan
    Write-Host "===================" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Usage: .\dd.ps1 <command> [options]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Commands:" -ForegroundColor Green
    Write-Host "  run            - Run the local development server and open in browser"
    Write-Host "  deploy         - Deploy to Google App Engine"
    Write-Host "  format         - Format all Python files with Black"
    Write-Host "  format --check - Check formatting without changes"
    Write-Host "  help           - Show this help message"
    Write-Host ""
    Write-Host "Deploy Options:" -ForegroundColor Green
    Write-Host "  -Project <name>   - GCP project ID (default: rethinkify)"
    Write-Host "  -Region <region>  - GAE region (default: europe-west1)"
    Write-Host "  -NoPromote        - Don't promote the new version"
    Write-Host "  -Version <ver>    - Specific version name"
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Green
    Write-Host "  .\dd.ps1 run"
    Write-Host "  .\dd.ps1 deploy"
    Write-Host "  .\dd.ps1 deploy -Project myproject -Version v2"
    Write-Host ""
}

function Get-VenvPath {
    # Check for .venv or venv (prefer .venv)
    if (Test-Path (Join-Path $ScriptRoot ".venv")) {
        return Join-Path $ScriptRoot ".venv"
    } elseif (Test-Path (Join-Path $ScriptRoot "venv")) {
        return Join-Path $ScriptRoot "venv"
    }
    return $null
}

function Get-VenvPython {
    $venvPath = Get-VenvPath
    if ($venvPath) {
        return Join-Path $venvPath "Scripts\python.exe"
    }
    return "python"
}

function Invoke-Format {
    $VenvPython = Get-VenvPython

    & $VenvPython -m black --version 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Black not found. Installing..." -ForegroundColor Yellow
        & $VenvPython -m pip install black
    }

    if ($Args -contains "--check") {
        Write-Host "Checking Python formatting..." -ForegroundColor Cyan
        & $VenvPython -m black --check web/
    } else {
        Write-Host "Formatting Python files..." -ForegroundColor Cyan
        & $VenvPython -m black web/
    }
}

function Invoke-Deploy {
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  Deploying Rethinkify to App Engine" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""

    $webDir = Join-Path $ScriptRoot "web"
    Push-Location $webDir

    try {
        if (-not (Get-Command "gcloud" -ErrorAction SilentlyContinue)) {
            Write-Host "ERROR: gcloud CLI is not installed or not in PATH" -ForegroundColor Red
            Write-Host "Install from: https://cloud.google.com/sdk/docs/install" -ForegroundColor Yellow
            exit 1
        }

        Write-Host "Project:   $Project" -ForegroundColor Yellow
        Write-Host "Region:    $Region" -ForegroundColor Yellow
        Write-Host "Directory: $webDir" -ForegroundColor Yellow
        Write-Host ""

        # Make sure App Engine is enabled and an app exists in the chosen region
        & gcloud services enable appengine.googleapis.com --project $Project | Out-Null

        & gcloud app describe --project $Project --format=json *> $null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "No App Engine app found for project $Project. Creating one in region $Region..." -ForegroundColor Yellow
            & gcloud app create --project $Project --region $Region --quiet
        }

        $deployArgs = @("app", "deploy", "app.yaml", "--project", $Project, "--quiet")

        if ($Version) {
            $deployArgs += "--version"
            $deployArgs += $Version
        }

        if ($NoPromote) {
            $deployArgs += "--no-promote"
        }

        Write-Host "Running: gcloud $($deployArgs -join ' ')" -ForegroundColor Gray
        Write-Host ""

        & gcloud @deployArgs

        if ($LASTEXITCODE -eq 0) {
            Write-Host ""
            Write-Host "========================================" -ForegroundColor Green
            Write-Host "  Deployment successful!" -ForegroundColor Green
            Write-Host "========================================" -ForegroundColor Green
            Write-Host ""
            Write-Host "View at: https://$Project.appspot.com" -ForegroundColor Cyan
        } else {
            Write-Host ""
            Write-Host "Deployment failed with exit code: $LASTEXITCODE" -ForegroundColor Red
            exit $LASTEXITCODE
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-Run {
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  Starting Rethinkify Dev Server" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""

    $venvPath = Get-VenvPath
    if (-not $venvPath) {
        Write-Host "Virtual environment not found." -ForegroundColor Yellow
        Write-Host "Creating .venv..." -ForegroundColor Yellow
        python -m venv (Join-Path $ScriptRoot ".venv")
        $venvPath = Join-Path $ScriptRoot ".venv"

        $pip = Join-Path $venvPath "Scripts\pip.exe"
        $reqFile = Join-Path $ScriptRoot "web\requirements.txt"
        if (Test-Path $reqFile) {
            Write-Host "Installing requirements..." -ForegroundColor Yellow
            & $pip install -r $reqFile
        }
    }

    $activateScript = Join-Path $venvPath "Scripts\Activate.ps1"
    . $activateScript

    $webDir = Join-Path $ScriptRoot "web"
    $env:PYTHONPATH = $webDir

    $port = 8081
    $url = "http://localhost:$port"

    Write-Host "Starting server at $url" -ForegroundColor Green
    Write-Host "Press Ctrl+C to stop" -ForegroundColor Gray
    Write-Host ""

    Start-Job -ScriptBlock { Start-Sleep -Seconds 2; Start-Process $using:url } | Out-Null

    Push-Location $webDir
    try {
        python main.py
    }
    finally {
        Pop-Location
    }
}

# Main
switch ($Command) {
    "deploy" { Invoke-Deploy }
    "run"    { Invoke-Run }
    "format" { Invoke-Format }
    "help"   { Show-Help }
    default  { Show-Help }
}
