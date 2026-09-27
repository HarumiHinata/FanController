$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class IconNative { [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr icon); }'
$bitmap = [Drawing.Bitmap]::new(64,64)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.Clear([Drawing.Color]::FromArgb(255,15,19,25))
$pen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(99,216,180),3)
$brush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(99,216,180))
$graphics.DrawEllipse($pen,5,5,54,54)
for($i=0;$i -lt 5;$i++) {
    $state=$graphics.Save()
    $graphics.TranslateTransform(32,32)
    $graphics.RotateTransform($i*72)
    $graphics.FillEllipse($brush,-5,-22,10,18)
    $graphics.Restore($state)
}
$graphics.FillEllipse($brush,26,26,12,12)
$handle=$bitmap.GetHicon()
$icon=[Drawing.Icon]::FromHandle($handle)
$destination=Join-Path $PSScriptRoot '../resources/fan.ico'
$stream=[IO.File]::Create($destination)
try { $icon.Save($stream) } finally { $stream.Dispose(); $icon.Dispose(); [void][IconNative]::DestroyIcon($handle); $pen.Dispose(); $brush.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
