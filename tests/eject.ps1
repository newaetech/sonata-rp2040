# param (
#     [string]$DriveName
# )

$DriveName = "SONATA"

$driveLetter = Get-WmiObject Win32_Volume | Where-Object { $_.Label -eq $DriveName } | Select-Object -ExpandProperty DriveLetter
if ($driveLetter -ne $null) {
    $driveEject = New-Object -comObject Shell.Application
    $driveEject.Namespace(17).ParseName("$driveLetter").InvokeVerb("Eject")
    Write-Host "Ejecting '$DriveName' "
} else {
    Write-Host "Drive '$DriveName' not found."
}
# $ejectResult = (New-Object -comObject Shell.Application).Namespace(17).ParseName("G:").InvokeVerb('Eject')
