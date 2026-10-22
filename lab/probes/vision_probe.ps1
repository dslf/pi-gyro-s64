# vision_probe.ps1 -- ask the local endpoint directly whether the model accepts image input
param(
  [string]$ImagePath = "lab/probes/hud_frame.png",
  [string]$BaseUrl = "http://127.0.0.1:8080/v1",
  [string]$Model = "qwen3.8-flash-next-gyro-gyro-s"
)
$b64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ImagePath))
$body = @{
  model = $Model
  max_tokens = 300
  messages = @(
    @{ role = "user"; content = @(
      @{ type = "text"; text = "Transcribe exactly the text rendered in this image. If there is no text, say NO TEXT." },
      @{ type = "image_url"; image_url = @{ url = "data:image/png;base64,$b64" } }
    ) }
  )
} | ConvertTo-Json -Depth 6
try {
  $resp = Invoke-RestMethod -Uri "$BaseUrl/chat/completions" -Method Post -ContentType "application/json" -Body $body -TimeoutSec 120
  $txt = $resp.choices[0].message.content
  Write-Output "--- answer ---"
  Write-Output $txt
  Write-Output "--- usage ---"
  Write-Output ("prompt_tokens=" + $resp.usage.prompt_tokens + " completion_tokens=" + $resp.usage.completion_tokens)
} catch {
  Write-Output "REQUEST FAILED: $($_.Exception.Message)"
  if ($_.Exception.Response) {
    $reader = New-Object IO.StreamReader($_.Exception.Response.GetResponseStream())
    Write-Output $reader.ReadToEnd()
  }
}
