# One-off importer: cuts the delivered contact sheets in assets\incoming into the
# individual PNGs the game and the store page expect.
#
#   .\tools\import_incoming.ps1
#
# Note the two sheets arrived with their names swapped -- incoming\portraits.jpg
# is the capsule sheet and incoming\capsules.jpg is the roster sheet. The
# mapping below is by content, not by filename.
#
# The logo sheet is a JPEG, so its transparency was flattened into a visible
# checkerboard. It is cropped here and keyed to alpha at load time instead
# (see Textures::loadEmissive), which also preserves the glow falloff.

param(
    [string]$Root = "$PSScriptRoot\..",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$incoming = Join-Path $Root "assets\incoming"
$assets = Join-Path $Root "assets"

function Save-Crop {
    param(
        [System.Drawing.Image]$Image,
        [int]$X, [int]$Y, [int]$W, [int]$H,
        [string]$Target
    )

    $dir = Split-Path $Target -Parent
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    if ((Test-Path $Target) -and -not $Force) {
        Write-Host ("  skip   {0}" -f (Split-Path $Target -Leaf))
        return
    }

    # Clamp so a slightly generous rect can never throw.
    $X = [math]::Max(0, $X); $Y = [math]::Max(0, $Y)
    $W = [math]::Min($W, $Image.Width - $X)
    $H = [math]::Min($H, $Image.Height - $Y)

    $bmp = New-Object System.Drawing.Bitmap($W, $H)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.DrawImage($Image,
            (New-Object System.Drawing.Rectangle(0, 0, $W, $H)),
            (New-Object System.Drawing.Rectangle($X, $Y, $W, $H)),
            [System.Drawing.GraphicsUnit]::Pixel)
    }
    finally { $g.Dispose() }

    $bmp.Save($Target, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host ("  write  {0,-26} {1} x {2}" -f (Split-Path $Target -Leaf), $W, $H)
}

function Open-Sheet {
    param([string]$Name)
    $path = Join-Path $incoming $Name
    if (-not (Test-Path $path)) { throw "missing $path" }
    return [System.Drawing.Image]::FromFile($path)
}

# Reads a whole sheet into a flat BGRA byte array once. Per-pixel GetPixel calls
# are far too slow in PowerShell for megapixel sheets.
function Read-Pixels {
    param([string]$Path)

    $bmp = New-Object System.Drawing.Bitmap($Path)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $bmp.Width, $bmp.Height)
    $data = $bmp.LockBits($rect,
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)

    $bytes = New-Object byte[] ($data.Stride * $data.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)

    $result = @{ Bytes = $bytes; Stride = $data.Stride; Width = $bmp.Width; Height = $bmp.Height }
    $bmp.UnlockBits($data)
    $bmp.Dispose()
    return $result
}

# Finds the bounding box of everything inside a cell that differs from the cell's
# corner colour -- i.e. the artwork, without its surrounding margin. Sampling
# every other pixel is plenty accurate and twice as fast.
function Get-ContentBounds {
    param($Pix, [int]$X, [int]$Y, [int]$W, [int]$H, [int]$Threshold = 26)

    $b = $Pix.Bytes; $s = $Pix.Stride

    $o = ($Y + 3) * $s + ($X + 3) * 4
    $bg0 = $b[$o]; $bg1 = $b[$o + 1]; $bg2 = $b[$o + 2]

    $minX = $X + $W; $minY = $Y + $H; $maxX = $X; $maxY = $Y
    $found = $false

    for ($py = $Y; $py -lt ($Y + $H); $py += 2) {
        $row = $py * $s
        for ($px = $X; $px -lt ($X + $W); $px += 2) {
            $i = $row + $px * 4
            $d = [math]::Abs($b[$i] - $bg0) + [math]::Abs($b[$i + 1] - $bg1) + [math]::Abs($b[$i + 2] - $bg2)
            if ($d -gt $Threshold) {
                if ($px -lt $minX) { $minX = $px }
                if ($px -gt $maxX) { $maxX = $px }
                if ($py -lt $minY) { $minY = $py }
                if ($py -gt $maxY) { $maxY = $py }
                $found = $true
            }
        }
    }

    if (-not $found) { return @{ X = $X; Y = $Y; W = $W; H = $H } }
    return @{ X = $minX; Y = $minY; W = ($maxX - $minX + 1); H = ($maxY - $minY + 1) }
}

# Grows a bounds box to a centred square, clamped inside the sheet. Board tiles
# and icons must be square or they stretch when drawn on a square grid.
function Expand-ToSquare {
    param($Bounds, [int]$SheetW, [int]$SheetH, [int]$Pad = 4)

    $side = [math]::Max($Bounds.W, $Bounds.H) + $Pad * 2
    $cx = $Bounds.X + [math]::Floor($Bounds.W / 2)
    $cy = $Bounds.Y + [math]::Floor($Bounds.H / 2)

    $x = $cx - [math]::Floor($side / 2)
    $y = $cy - [math]::Floor($side / 2)

    $x = [math]::Max(0, [math]::Min($x, $SheetW - $side))
    $y = [math]::Max(0, [math]::Min($y, $SheetH - $side))
    $side = [math]::Min($side, [math]::Min($SheetW, $SheetH))

    return @{ X = [int]$x; Y = [int]$y; W = [int]$side; H = [int]$side }
}

# ---------------------------------------------------------------- portraits --
# assets\incoming\capsules.jpg is really the 3x3 roster sheet (896 x 1200).
# Cells are 298 x 400; each is inset slightly to drop the dark gutter.

Write-Host "`nroster portraits"
$sheet = Open-Sheet "capsules.jpg"
try {
    $cw = [math]::Floor($sheet.Width / 3)
    $ch = [math]::Floor($sheet.Height / 3)
    $inset = 10

    # column, row  ->  which variant reads best
    $picks = @(
        @{ c = 0; r = 0; name = "snake_viper" }      # motion streaks suit the speed fantasy
        @{ c = 0; r = 1; name = "snake_bulwark" }    # larger, stronger hex shield
        @{ c = 2; r = 0; name = "snake_wraith" }     # plain background, no competing grid
        @{ c = 2; r = 1; name = "snake_midas" }      # sparkles
        @{ c = 2; r = 2; name = "snake_ouroboros" }  # only variant
    )

    foreach ($p in $picks) {
        Save-Crop -Image $sheet `
            -X (($p.c * $cw) + $inset) -Y (($p.r * $ch) + $inset) `
            -W ($cw - $inset * 2) -H ($ch - $inset * 2) `
            -Target (Join-Path $assets "portraits\$($p.name).png")
    }
}
finally { $sheet.Dispose() }

# ------------------------------------------------------------ board objects --
# 1408 x 768, 4 columns x 2 rows. Row 2 columns 0-1 duplicate row 1 columns 2-3.

Write-Host "`nboard objects"
$objectsPath = Join-Path $incoming "objects.jpg"
$pix = Read-Pixels $objectsPath
$sheet = Open-Sheet "objects.jpg"
try {
    $cw = [math]::Floor($sheet.Width / 4)
    $ch = [math]::Floor($sheet.Height / 2)

    # The sheet's tiles are not on an even quarter split -- they sit on a 336 px
    # pitch starting at x=48, so measured rects are used rather than a grid.
    # Each is square, which board tiles must be.
    $side = 312
    $colX = @(48, 384, 718, 1052)
    $rowY = @(48, 398)

    $picks = @(
        @{ c = 0; r = 0; name = "food_normal" }
        @{ c = 1; r = 0; name = "food_bonus" }
        @{ c = 2; r = 0; name = "wall" }
        @{ c = 3; r = 0; name = "border" }
        @{ c = 2; r = 1; name = "sentinel" }
        @{ c = 3; r = 1; name = "shield" }
    )

    foreach ($p in $picks) {
        Save-Crop -Image $sheet -X $colX[$p.c] -Y $rowY[$p.r] -W $side -H $side `
            -Target (Join-Path $assets "objects\$($p.name).png")
    }
}
finally { $sheet.Dispose() }

# -------------------------------------------------------------- backgrounds --
# 1376 x 768. Left panel is the bright green plate; the two on the right are the
# quiet ones that UI can actually sit on.

Write-Host "`nbackground plates"
$sheet = Open-Sheet "backgrounds.jpg"
try {
    Save-Crop -Image $sheet -X 0   -Y 0   -W 686 -H 768 -Target (Join-Path $assets "ui\bg_title.png")
    # Inset well clear of the sheet's white gutters -- a sliver of gutter reads
    # as a bright rectangle floating over the menu.
    Save-Crop -Image $sheet -X 748 -Y 12  -W 616 -H 344 -Target (Join-Path $assets "ui\bg_menu.png")
    Save-Crop -Image $sheet -X 748 -Y 408 -W 592 -H 326 -Target (Join-Path $assets "ui\bg_menu_alt.png")
}
finally { $sheet.Dispose() }

# --------------------------------------------------------------------- logo --
# Cropped to the wordmark only (no reflection) for in-game use, plus a full
# version with the reflection for the store.

Write-Host "`nlogo"
$sheet = Open-Sheet "logo.jpg"
try {
    Save-Crop -Image $sheet -X 96 -Y 286 -W 1180 -H 180 -Target (Join-Path $assets "ui\logo.png")
    Save-Crop -Image $sheet -X 96 -Y 286 -W 1180 -H 330 -Target (Join-Path $assets "marketing\logo_reflected.png")
}
finally { $sheet.Dispose() }

# --------------------------------------------------------------------- icon --

Write-Host "`nicon"
$sheet = Open-Sheet "icon.jpg"
try {
    Save-Crop -Image $sheet -X 0 -Y 0 -W $sheet.Width -H $sheet.Height `
        -Target (Join-Path $assets "ui\icon.png")
}
finally { $sheet.Dispose() }

# ----------------------------------------------------------------- capsules --
# 1440 x 720, irregular layout with grey gutters. These rects are eyeballed and
# are a starting point for the store page, not final crops.

Write-Host "`nstore capsules (approximate crops)"
$sheet = Open-Sheet "portraits.jpg"
try {
    $capsules = @(
        @{ x = 10;   y = 14;  w = 582; h = 331; name = "capsule_hero" }
        @{ x = 604;  y = 22;  w = 531; h = 226; name = "capsule_header" }
        @{ x = 1140; y = 14;  w = 300; h = 336; name = "capsule_vertical" }
        @{ x = 604;  y = 268; w = 316; h = 117; name = "capsule_small" }
        @{ x = 22;   y = 368; w = 283; h = 344; name = "capsule_library" }
        @{ x = 604;  y = 400; w = 836; h = 312; name = "capsule_wide" }
    )

    foreach ($c in $capsules) {
        Save-Crop -Image $sheet -X $c.x -Y $c.y -W $c.w -H $c.h `
            -Target (Join-Path $assets "marketing\$($c.name).png")
    }
}
finally { $sheet.Dispose() }

Write-Host "`ndone."
