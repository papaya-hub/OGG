param(
    [string]$Prompt = "five stax gaming party"
)

$ErrorActionPreference = "Stop"
$settingsPath = Join-Path $env:LOCALAPPDATA "OffGridGames\appsettings.json"
if (-not (Test-Path $settingsPath)) {
    Write-Error "appsettings.json not found"
    exit 1
}

$cfg = Get-Content $settingsPath -Raw | ConvertFrom-Json
$openaiKey = [string]$cfg.ai.openai_api_key
$geminiKey = [string]$cfg.ai.gemini_api_key
$openaiModel = if ($cfg.ai.openai_image_model) { $cfg.ai.openai_image_model } else { "gpt-image-2" }
$openaiQuality = if ($cfg.ai.openai_image_quality) { $cfg.ai.openai_image_quality } else { "medium" }
$openaiSize = if ($cfg.ai.openai_image_size) { $cfg.ai.openai_image_size } else { "1536x1024" }
$geminiModel = if ($cfg.ai.gemini_image_model) { $cfg.ai.gemini_image_model } else { "gemini-2.5-flash-image" }

$outDir = Join-Path $PSScriptRoot "..\..\build\test_ai_images"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

function Save-Base64Image([string]$Base64, [string]$Path) {
    $bytes = [Convert]::FromBase64String($Base64)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Invoke-JsonPost([string]$Uri, [hashtable]$Headers, [string]$JsonBody, [int]$Retries = 6) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($JsonBody)
    for ($i = 0; $i -lt $Retries; $i++) {
        try {
            return Invoke-RestMethod -Uri $Uri -Method Post -Headers $Headers -ContentType "application/json; charset=utf-8" -Body $bytes
        } catch {
            $msg = $_.Exception.Message
            if ($msg -match '429' -and $i + 1 -lt $Retries) {
                $wait = 20 * ($i + 1)
                Write-Host "rate limited, retrying in ${wait}s..."
                Start-Sleep -Seconds $wait
                continue
            }
            throw
        }
    }
    throw "request failed after retries"
}

$openaiOut = Join-Path $outDir "openai_five_stax.png"
$geminiOut = Join-Path $outDir "gemini_five_stax.png"
$ok = $true

if ([string]::IsNullOrWhiteSpace($openaiKey)) {
    Write-Host "openai: no key configured"
    $ok = $false
} else {
    try {
        $body = @{
            model = $openaiModel
            prompt = $Prompt
            n = 1
            size = $openaiSize
            quality = $openaiQuality
        } | ConvertTo-Json -Compress
        $resp = Invoke-JsonPost `
            -Uri "https://api.openai.com/v1/images/generations" `
            -Headers @{ Authorization = "Bearer $openaiKey" } `
            -JsonBody $body
        $b64 = $resp.data[0].b64_json
        if (-not $b64) { throw "OpenAI response missing b64_json" }
        Save-Base64Image $b64 $openaiOut
        Write-Host "openai: ok -> $openaiOut"
    } catch {
        Write-Host "openai: failed - $($_.Exception.Message)"
        $ok = $false
    }
}

if ([string]::IsNullOrWhiteSpace($geminiKey)) {
    Write-Host "gemini: no key configured"
    $ok = $false
} else {
    try {
        if ($geminiModel -match '^imagen') {
            $body = @{
                instances = @(@{ prompt = $Prompt })
                parameters = @{ sampleCount = 1; aspectRatio = "4:3" }
            } | ConvertTo-Json -Compress -Depth 5
            $uri = "https://generativelanguage.googleapis.com/v1beta/models/${geminiModel}:predict"
            $resp = Invoke-JsonPost -Uri $uri -Headers @{ "x-goog-api-key" = $geminiKey } -JsonBody $body
            $b64 = $resp.predictions[0].bytesBase64Encoded
        } else {
            $body = @{
                contents = @(@{ parts = @(@{ text = $Prompt }) })
                generationConfig = @{ responseModalities = @("TEXT", "IMAGE") }
            } | ConvertTo-Json -Compress -Depth 6
            $uri = "https://generativelanguage.googleapis.com/v1beta/models/${geminiModel}:generateContent"
            $resp = Invoke-JsonPost -Uri $uri -Headers @{ "x-goog-api-key" = $geminiKey } -JsonBody $body
            $b64 = $resp.candidates[0].content.parts | Where-Object { $_.inlineData } | Select-Object -First 1 -ExpandProperty inlineData | Select-Object -ExpandProperty data
        }
        if (-not $b64) { throw "Gemini response missing image data" }
        Save-Base64Image $b64 $geminiOut
        Write-Host "gemini: ok -> $geminiOut"
    } catch {
        Write-Host "gemini: failed - $($_.Exception.Message)"
        $ok = $false
    }
}

if (-not $ok) { exit 1 }
