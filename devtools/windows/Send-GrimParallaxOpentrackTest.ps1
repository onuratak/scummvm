param(
	[int]$Port = 4242,
	[string]$Address = "127.0.0.1",
	[int]$Hz = 60,
	[double]$AmplitudeX = 6.0,
	[double]$AmplitudeY = 3.0,
	[double]$AmplitudeZ = 0.0,
	[double]$Yaw = 0.0,
	[double]$Pitch = 0.0,
	[double]$Roll = 0.0,
	[double]$PeriodSeconds = 6.0,
	[int]$DurationSeconds = 0
)

$udp = [System.Net.Sockets.UdpClient]::new()
$endpoint = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Parse($Address), $Port)
$intervalMs = [Math]::Max(1, [int](1000 / [Math]::Max(1, $Hz)))
$start = Get-Date

try {
	while ($true) {
		$elapsed = ((Get-Date) - $start).TotalSeconds
		if ($DurationSeconds -gt 0 -and $elapsed -ge $DurationSeconds) {
			break
		}

		$phase = ($elapsed / [Math]::Max(0.1, $PeriodSeconds)) * ([Math]::PI * 2.0)
		$pose = [double[]]@(
			([Math]::Sin($phase) * $AmplitudeX),
			([Math]::Cos($phase * 0.7) * $AmplitudeY),
			([Math]::Sin($phase * 0.35) * $AmplitudeZ),
			$Yaw,
			$Pitch,
			$Roll
		)

		$payload = [byte[]]::new(48)
		for ($i = 0; $i -lt $pose.Length; $i++) {
			[BitConverter]::GetBytes([double]$pose[$i]).CopyTo($payload, $i * 8)
		}

		[void]$udp.Send($payload, $payload.Length, $endpoint)
		Start-Sleep -Milliseconds $intervalMs
	}
}
finally {
	$udp.Dispose()
}
