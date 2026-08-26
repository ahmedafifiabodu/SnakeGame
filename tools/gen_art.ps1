# Generates NEON COIL marketing art via Pollinations (no API key required).
#
#   .\tools\gen_art.ps1                 # generate anything missing
#   .\tools\gen_art.ps1 -Only hero      # regenerate one asset
#   .\tools\gen_art.ps1 -Force          # regenerate everything
#   .\tools\gen_art.ps1 -Only hero -Seed 42
#
# SCOPE: store pages, README and social only. Gameplay visuals are drawn
# procedurally by the renderer -- the player picks their snake colour at runtime,
# so baked sprites would need 40 variants and still could not tint for the
# phase / dash / shield states. See docs/ASSETS.md.
#
# Seeds are pinned per asset so a rerun reproduces the same image. Pass -Seed to
# roll a variant of a single asset without disturbing the rest.
#
# Prompts are deliberately SHORT. Long prompts with hex codes measurably diluted
# the result on this model: colours are named in words, style words come first.

param(
    [string]$Only = "",
    [int]$Seed = 0,
    [switch]$Force,
    [string]$Model = "flux",
    [string]$OutDir = "$PSScriptRoot\..\assets\marketing"
)

$ErrorActionPreference = "Stop"

# Short shared style tail. Anything longer than this starts costing subject fidelity.
$Style = "neon arcade game art, dark navy background, bold clean shapes, high contrast, glowing rim light, no text"

$Assets = @(
    @{ Name = "hero"; W = 1536; H = 512; Seed = 101;
       Prompt = "Top down neon arcade arena. A glowing emerald green snake of square " +
                "segments coils diagonally across a dark tiled grid. A glowing coral red " +
                "orb nearby. Cinematic wide banner" }

    @{ Name = "capsule_main"; W = 1232; H = 706; Seed = 102;
       Prompt = "Top down view of a glowing emerald green segmented snake coiled into a " +
                "tight spiral, centered on a dark tiled arena floor, concentric glowing " +
                "wall rings, empty space at the top" }

    @{ Name = "capsule_small"; W = 920; H = 430; Seed = 103;
       Prompt = "Emerald green segmented snake head on the left, glowing coral red orb on " +
                "the right, dark tiled arena between them, wide banner, empty centre" }

    @{ Name = "capsule_vertical"; W = 748; H = 896; Seed = 104;
       Prompt = "Tall poster. A glowing emerald green segmented snake rising vertically " +
                "through a dark neon arena of glowing blocks, viewed top down, dramatic" }

    @{ Name = "icon"; W = 512; H = 512; Seed = 105;
       Prompt = "Bold minimal icon. A neon green segmented snake coiled into a thick " +
                "spiral ring, glowing, centered on near black, heavy silhouette" }

    @{ Name = "menu_background"; W = 1920; H = 1080; Seed = 106;
       Prompt = "Empty dark navy arena floor of faint glowing grid tiles receding into " +
                "blackness, scattered dim blocks, heavy vignette, very low contrast, " +
                "uncluttered background plate" }

    # Roster art -- used on the store page and the press kit, not in game.
    @{ Name = "snake_viper"; W = 768; H = 1024; Seed = 201;
       Prompt = "Top down portrait of a slender fast emerald green segmented snake in an " +
                "S curve, motion streaks trailing behind it, centered, radial glow" }

    @{ Name = "snake_bulwark"; W = 768; H = 1024; Seed = 202;
       Prompt = "Top down portrait of a thick heavily armoured cobalt blue segmented " +
                "snake in an S curve, metal plate scales, hexagonal shield aura, centered" }

    @{ Name = "snake_wraith"; W = 768; H = 1024; Seed = 203;
       Prompt = "Top down portrait of a ghostly translucent pale aqua segmented snake in " +
                "an S curve, tail dissolving into mist, floor faintly visible through it" }

    @{ Name = "snake_midas"; W = 768; H = 1024; Seed = 204;
       Prompt = "Top down portrait of an ornate gilded gold segmented snake in an S " +
                "curve, faceted gem like segments, gold sparks drifting upward, centered" }

    @{ Name = "snake_ouroboros"; W = 768; H = 1024; Seed = 205;
       Prompt = "Top down portrait of a banded orchid purple segmented snake curled into " +
                "a ring so its head almost meets its tail, alternating light and dark" }
)

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$resolved = (Resolve-Path $OutDir).Path
Write-Host "Output: $resolved`n"

$made = 0; $skipped = 0; $failed = 0

foreach ($asset in $Assets) {
    if ($Only -and $asset.Name -ne $Only) { continue }

    $path = Join-Path $resolved "$($asset.Name).png"

    if ((Test-Path $path) -and -not $Force -and $Seed -eq 0) {
        Write-Host ("  skip   {0}" -f $asset.Name)
        $skipped++
        continue
    }

    $useSeed = if ($Seed -ne 0) { $Seed } else { $asset.Seed }
    $full = "$($asset.Prompt). $Style"
    $encoded = [System.Uri]::EscapeDataString($full)
    $url = "https://image.pollinations.ai/prompt/$encoded" +
           "?width=$($asset.W)&height=$($asset.H)&model=$Model&seed=$useSeed&nologo=true"

    Write-Host ("  gen    {0}  ({1}x{2})" -f $asset.Name, $asset.W, $asset.H) -NoNewline

    try {
        Invoke-WebRequest -Uri $url -OutFile $path -TimeoutSec 300 -UseBasicParsing
        $kb = [math]::Round((Get-Item $path).Length / 1KB)
        Write-Host "  -> ${kb} KB"
        $made++
    }
    catch {
        Write-Host "  -> FAILED: $($_.Exception.Message)"
        if (Test-Path $path) { Remove-Item $path -Force }
        $failed++
    }
}

Write-Host "`ngenerated $made, skipped $skipped, failed $failed"
if ($failed -gt 0) { Write-Host "rerun to retry failures (existing files are skipped)" }
