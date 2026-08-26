# Slices a contact sheet into individual asset PNGs.
#
# The image models return grids of variants rather than single files. This cuts
# them up without needing Photoshop, using System.Drawing (ships with Windows).
#
#   # uniform grid, left-to-right, top-to-bottom
#   .\tools\slice_sheet.ps1 -Source assets\incoming\portraits.png -Grid 3x3 `
#       -Out assets\portraits -Names viper_a,bulwark_a,wraith_a,bulwark_b,wraith_b,midas_a,viper_b,midas_b,ouroboros_a
#
#   # explicit rectangles, for sheets with uneven gutters
#   .\tools\slice_sheet.ps1 -Source assets\incoming\capsules.png `
#       -Rects "0,0,584,330;600,0,776,180" -Out assets\marketing -Names main,header
#
#   # just report the dimensions so the rects can be worked out
#   .\tools\slice_sheet.ps1 -Source assets\incoming\capsules.png -Inspect

param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$Grid = "",              # e.g. "3x3" (colsXrows)
    [string]$Rects = "",             # "x,y,w,h;x,y,w,h;..."
    [string]$Out = "assets\sliced",
    [string[]]$Names = @(),
    [int]$Gutter = 0,                # pixels trimmed off every edge of each cell
    [switch]$Inspect
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$sourcePath = (Resolve-Path $Source).Path
$image = [System.Drawing.Image]::FromFile($sourcePath)

try {
    if ($Inspect -or (-not $Grid -and -not $Rects)) {
        Write-Host ("{0}`n  {1} x {2} px" -f $sourcePath, $image.Width, $image.Height)
        if (-not $Inspect) { Write-Host "`nPass -Grid <colsXrows> or -Rects to slice." }
        return
    }

    $regions = @()

    if ($Grid) {
        if ($Grid -notmatch '^(\d+)[xX](\d+)$') { throw "-Grid must look like 3x3 (colsXrows)" }
        $cols = [int]$Matches[1]
        $rows = [int]$Matches[2]

        $cellW = [math]::Floor($image.Width / $cols)
        $cellH = [math]::Floor($image.Height / $rows)

        for ($r = 0; $r -lt $rows; $r++) {
            for ($c = 0; $c -lt $cols; $c++) {
                $regions += , @(
                    ($c * $cellW) + $Gutter,
                    ($r * $cellH) + $Gutter,
                    $cellW - ($Gutter * 2),
                    $cellH - ($Gutter * 2)
                )
            }
        }
    }
    else {
        foreach ($spec in $Rects -split ';') {
            if (-not $spec.Trim()) { continue }
            $parts = $spec -split ',' | ForEach-Object { [int]$_.Trim() }
            if ($parts.Count -ne 4) { throw "each rect must be x,y,w,h -- got '$spec'" }
            $regions += , $parts
        }
    }

    New-Item -ItemType Directory -Force -Path $Out | Out-Null
    $outDir = (Resolve-Path $Out).Path

    for ($i = 0; $i -lt $regions.Count; $i++) {
        $r = $regions[$i]
        $name = if ($i -lt $Names.Count) { $Names[$i] } else { "cell_{0:D2}" -f $i }
        $target = Join-Path $outDir "$name.png"

        $cropped = New-Object System.Drawing.Bitmap($r[2], $r[3])
        $graphics = [System.Drawing.Graphics]::FromImage($cropped)
        try {
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.DrawImage($image,
                (New-Object System.Drawing.Rectangle(0, 0, $r[2], $r[3])),
                (New-Object System.Drawing.Rectangle($r[0], $r[1], $r[2], $r[3])),
                [System.Drawing.GraphicsUnit]::Pixel)
        }
        finally { $graphics.Dispose() }

        $cropped.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
        $cropped.Dispose()

        Write-Host ("  {0,-18} {1} x {2}" -f "$name.png", $r[2], $r[3])
    }

    Write-Host "`n$($regions.Count) file(s) -> $outDir"
}
finally { $image.Dispose() }
