param([Parameter(Mandatory=$true)][string[]]$Documents,
      [string]$OutputRoot = (Join-Path $PSScriptRoot '..\out\delivery-docs\renders'))
$ErrorActionPreference='Stop'
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
# Use a dedicated native Word instance, never an existing user's document.
$word=New-Object -ComObject Word.Application
$word.Visible=$false
$word.DisplayAlerts=0
try {
    foreach($documentPath in $Documents) {
        $documentPath=[IO.Path]::GetFullPath($documentPath)
        $name=[IO.Path]::GetFileNameWithoutExtension($documentPath)
        $folder=Join-Path $OutputRoot $name
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        $doc=$word.Documents.Open($documentPath,$false,$true)
        try {
            $doc.Repaginate()
            $pdf=Join-Path $folder ($name+'.pdf')
            $doc.ExportAsFixedFormat($pdf,17)
            $pages=$doc.ComputeStatistics(2)
            [pscustomobject]@{File=$documentPath;PDF=$pdf;Pages=$pages} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $folder 'render.json') -Encoding utf8
        } finally { $doc.Close(0) }
        & pdftoppm.exe -r 110 -png $pdf (Join-Path $folder 'page')
        if($LASTEXITCODE -ne 0){throw "Rasterization failed: $documentPath"}
        Write-Output "$name rendered ($pages pages)"
    }
} finally {
    $word.Quit()
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($word)
}
