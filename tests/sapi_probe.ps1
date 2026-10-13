# Probes a SAPI 5 voice the way a screen reader drives it and prints numbers to
# compare: how <rate>/<volume> markup combines with SetRate/SetVolume (JAWS sends
# <rate AbsSpeed>, <volume level>, <pitch AbsMiddle> with every phrase), whether
# '<' in plain text swallows the rest of the phrase, and how each phrase ends
# (a last sample far from zero is an audible click).
#
#   powershell -File tests\sapi_probe.ps1 -Voice "ruTTS"
#   powershell -File tests\sapi_probe.ps1 -Voice "ruTTS" -Dll build\x64\bin\rutts_sapi.dll
#
# -Dll speaks through that build instead of the installed one: a COM override in
# HKCU, removed again on exit. Audio is 11kHz16BitMono, the format JAWS forces
# through sapi5x.ini (Output=10).
param([string]$Voice = 'ruTTS', [string]$Dll = '')

$token = Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Speech\Voices\Tokens' |
    Where-Object { (Get-ItemProperty $_.PSPath).'(default)' -eq $Voice } | Select-Object -First 1
if (-not $token) { throw "No voice token named '$Voice'" }
$clsid = (Get-ItemProperty $token.PSPath).CLSID
$override = "HKCU:\Software\Classes\CLSID\$clsid"
if ($Dll) {
    New-Item -Force "$override\InprocServer32" | Out-Null
    Set-ItemProperty "$override\InprocServer32" -Name '(default)' -Value (Resolve-Path $Dll).Path
    Set-ItemProperty "$override\InprocServer32" -Name 'ThreadingModel' -Value 'Both'
}

function Speak-ToBytes($v, [string]$text, [int]$flags) {
    $ms = New-Object -ComObject SAPI.SpMemoryStream
    $ms.Format.Type = 10
    $v.AudioOutputStream = $ms
    [void]$v.Speak($text, $flags)
    return [byte[]]$ms.GetData()
}
function Get-Samples([byte[]]$b) {
    $s = New-Object 'int[]' ($b.Length / 2)
    for ($i = 0; $i -lt $s.Length; $i++) { $s[$i] = [BitConverter]::ToInt16($b, 2 * $i) }
    return , $s
}

try {
    $v = New-Object -ComObject SAPI.SpVoice
    $v.Voice = $v.GetVoices() | Where-Object { $_.GetDescription() -eq $Voice }
    $phrase = 'Проверка скорости речи, раз два три.'
    $cases = @(
        @('plain',             0, 100, $phrase, 8),
        @('xml absspeed=+5',   0, 100, "<rate absspeed=""5""/>$phrase", 8),
        @('xml absspeed=-5',   0, 100, "<rate absspeed=""-5""/>$phrase", 8),
        @('SetRate +5',        5, 100, $phrase, 8),
        @('SetRate +5, xml -5',5, 100, "<rate absspeed=""-5""/>$phrase", 8),
        @('xml volume=30',     0, 100, "<volume level=""30""/>$phrase", 8),
        @('SetVolume 30',      0, 30,  $phrase, 8),
        @('text a меньше b',   0, 100, 'если a меньше b то дальше идёт длинный текст', 16),
        @('text a < b',        0, 100, 'если a < b то дальше идёт длинный текст', 16)
    )
    'Rate/volume markup and ''<'' (sum: SetRate+xml = 0 matches plain; absolute: matches xml -5):'
    foreach ($c in $cases) {
        $v.Rate = $c[1]; $v.Volume = $c[2]
        $s = Get-Samples (Speak-ToBytes $v $c[3] $c[4])
        $peak = 0; foreach ($x in $s) { if ([Math]::Abs($x) -gt $peak) { $peak = [Math]::Abs($x) } }
        '  {0,-20} {1,5} ms  peak {2,5}' -f $c[0], [int]($s.Length / 11.025), $peak
    }
    $v.Rate = 0; $v.Volume = 100

    'Phrase ends (JAWS markup, bookmark after the word): last sample, max |last 10|:'
    foreach ($word in 'Файл', 'Правка', 'Вид', 'кнопка', 'список', 'д', 'м') {
        $s = Get-Samples (Speak-ToBytes $v "<pitch absmiddle=""0""/><rate absspeed=""0""/>$word<bookmark mark=""1""/>" 8)
        $end = $s.Length - 1
        while ($end -gt 0 -and $s[$end] -eq 0) { $end-- }   # trailing padding hides nothing
        $tail = 0; for ($i = [Math]::Max(0, $end - 9); $i -le $end; $i++) { $tail = [Math]::Max($tail, [Math]::Abs($s[$i])) }
        '  {0,-8} last sound sample {1,6}  max|last 10| {2,5}  zero padding {3} samples' -f $word, $s[$end], $tail, ($s.Length - 1 - $end)
    }
} finally {
    if ($Dll) { Remove-Item -Recurse -Force $override }
}
