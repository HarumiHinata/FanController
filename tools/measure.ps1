param(
    [Parameter(Mandatory)][int]$TargetProcessId,
    [double]$DurationHours=72,
    [int]$WarmupSeconds=300,
    [int]$IntervalSeconds=5,
    [Parameter(Mandatory)][string]$OutputPrefix
)
$ErrorActionPreference='Stop'
if($DurationHours -le 0 -or $DurationHours -gt 168 -or $IntervalSeconds -lt 1) { throw 'Invalid measurement interval.' }
$target=Get-Process -Id $TargetProcessId
$targetPath=$target.Path
$identityStart=$target.StartTime.ToUniversalTime()
$target.Dispose()
$buildHash=(Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash
$directory=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPrefix))
[IO.Directory]::CreateDirectory($directory) | Out-Null
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class FanMeasureNative { [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flag); }'
$writer=[IO.StreamWriter]::new("$OutputPrefix.csv",$false,[Text.UTF8Encoding]::new($false))
$writer.WriteLine('utc,elapsedSeconds,observedSeconds,cpuPercent,privateBytes,workingSet,handles,gdi,user,threads,processes')
$clock=[Diagnostics.Stopwatch]::StartNew()
$previousTime=0.0; $observed=0.0; $gaps=0; $samples=0
$cpuSeconds=0.0; $measuredSeconds=0.0; $peakPrivate=0L; $peakWorking=0L
$previousCpu=@{}; $first=[Collections.Generic.List[double]]::new(); $last=[Collections.Generic.Queue[double]]::new()
$state='running'; $errorMessage=$null; $nextSummary=0.0; $initial=$null; $latest=$null
$logicalProcessors=[Environment]::ProcessorCount
function Save-Summary {
    $firstMedian=$null; $lastMedian=$null
    if($first.Count) { $sorted=@($first | Sort-Object); $firstMedian=$sorted[[int][Math]::Floor($sorted.Count/2)] }
    if($last.Count) { $sorted=@($last.ToArray() | Sort-Object); $lastMedian=$sorted[[int][Math]::Floor($sorted.Count/2)] }
    $summary=[ordered]@{
        status=$state; updatedUtc=[DateTime]::UtcNow.ToString('o'); targetPid=$TargetProcessId;
        executable=$targetPath; sha256=$buildHash; durationTargetHours=$DurationHours;
        elapsedSeconds=[Math]::Round($clock.Elapsed.TotalSeconds,2); observedSeconds=[Math]::Round($observed,2);
        warmupSeconds=$WarmupSeconds; measuredSeconds=[Math]::Round($measuredSeconds,2); samples=$samples; samplingGaps=$gaps;
        meanCpuPercent=if($measuredSeconds -gt 0){[Math]::Round(100*$cpuSeconds/$measuredSeconds/$logicalProcessors,5)}else{$null};
        peakPrivateBytes=$peakPrivate; peakWorkingSetBytes=$peakWorking; firstMedianPrivateBytes=$firstMedian;
        lastMedianPrivateBytes=$lastMedian; initial=$initial; latest=$latest; error=$errorMessage
    }
    $temp="$OutputPrefix.json.tmp"
    [IO.File]::WriteAllText($temp,($summary | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temp -Destination "$OutputPrefix.json" -Force
}
try {
    while($observed -lt $DurationHours*3600) {
        $primary=Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
        if(-not $primary -or $primary.StartTime.ToUniversalTime() -ne $identityStart) { $state='interrupted'; $errorMessage='The measured application exited. No process was restarted.'; break }
        $primary.Dispose()
        $now=$clock.Elapsed.TotalSeconds; $delta=$now-$previousTime; $previousTime=$now
        if($delta -gt [Math]::Max(30,$IntervalSeconds*3)) { $gaps++ }
        $observed+=[Math]::Min($delta,$IntervalSeconds*2)
        $allProcesses=@(Get-Process -Name FanController -ErrorAction SilentlyContinue)
        $processes=@($allProcesses | Where-Object {$_.Path -eq $targetPath})
        $private=0L; $working=0L; $handles=0; $gdi=0; $user=0; $threads=0; $deltaCpu=0.0; $currentCpu=@{}
        foreach($process in $processes) {
            $process.Refresh(); $private+=$process.PrivateMemorySize64; $working+=$process.WorkingSet64; $handles+=$process.HandleCount; $threads+=$process.Threads.Count
            $gdi+=[FanMeasureNative]::GetGuiResources($process.Handle,0); $user+=[FanMeasureNative]::GetGuiResources($process.Handle,1)
            $cpu=$process.TotalProcessorTime.TotalSeconds; $currentCpu[$process.Id]=$cpu
            if($previousCpu.ContainsKey($process.Id)) { $deltaCpu+=[Math]::Max(0,$cpu-$previousCpu[$process.Id]) } elseif($samples -gt 0) { $deltaCpu+=$cpu }
        }
        $previousCpu=$currentCpu
        foreach($process in $allProcesses) { $process.Dispose() }
        $cpuPercent=if($delta -gt 0){100*$deltaCpu/$delta/$logicalProcessors}else{0}
        $latest=[ordered]@{privateBytes=$private;workingSet=$working;handles=$handles;gdi=$gdi;user=$user;threads=$threads;processes=$processes.Count}
        if($observed -ge $WarmupSeconds) {
            if(-not $initial){$initial=$latest}
            $cpuSeconds+=$deltaCpu; $measuredSeconds+=$delta; $peakPrivate=[Math]::Max($peakPrivate,$private); $peakWorking=[Math]::Max($peakWorking,$working)
            if($first.Count -lt 60){$first.Add($private)}
            $last.Enqueue($private); if($last.Count -gt 60){[void]$last.Dequeue()}
        }
        $samples++
        $writer.WriteLine(('{0},{1:F3},{2:F3},{3:F6},{4},{5},{6},{7},{8},{9},{10}' -f [DateTime]::UtcNow.ToString('o'),$now,$observed,$cpuPercent,$private,$working,$handles,$gdi,$user,$threads,$processes.Count))
        if($now -ge $nextSummary) { $writer.Flush(); Save-Summary; $nextSummary=$now+60 }
        Start-Sleep -Seconds $IntervalSeconds
    }
    if($state -eq 'running') { $state=if($gaps){'completed_with_gaps'}else{'completed'} }
} catch { $state='failed'; $errorMessage=$_.Exception.Message }
finally { $writer.Dispose(); Save-Summary }
