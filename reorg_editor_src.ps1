# Reorganize editor/src into grouped folders to match CMakeLists.txt.
# Run once from the repo root:  D:\C++_Projects\3DGEngine
#   powershell -ExecutionPolicy Bypass -File .\reorg_editor_src.ps1
# Idempotent: safe to re-run. Only .cpp files move; headers stay flat in include/.
# Git detects the renames when you `git add -A`, so history is preserved.

$ErrorActionPreference = 'Stop'
Set-Location -Path $PSScriptRoot

$map = [ordered]@{
    'app'         = @('main.cpp','EditorApp.cpp','EditorLog.cpp','EditorProject.cpp','EditorPanels.cpp','EditorDockspace.cpp')
    'controllers' = @('EditorGizmo.cpp','EditorDragDrop.cpp','EditorContentController.cpp','EditorCameraController.cpp','EditorMouseController.cpp','EditorRuntimeController.cpp','EditorTransformController.cpp')
    'scene'       = @('EditorScene.cpp','RuntimeSceneExporter.cpp')
    'assets'      = @('EditorAssets.cpp','CharacterAsset.cpp','AnimationClipAsset.cpp','AnimationGraphAsset.cpp','ParticleAsset.cpp')
    'panels'      = @('BehaviorGraphPanel.cpp','ParticleEditorPanel.cpp','ShaderEditorPanel.cpp','HudEditorPanel.cpp','CharacterEditorPanel.cpp','ClipEditorPanel.cpp','AnimationGraphEditorPanel.cpp')
    'render'      = @('EditorViewport.cpp','EditorLineRenderer.cpp')
    'scripting'   = @('EditorScriptTools.cpp','EditorGeneratedScriptTools.cpp','GameBtScripts.cpp','ScriptCompilerHelper.cpp')
    'platform'    = @('NativeDialog.cpp')
}

$moved = 0; $already = 0; $missing = @()
foreach ($folder in $map.Keys) {
    $dest = Join-Path 'editor\src' $folder
    if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Path $dest | Out-Null }
    foreach ($file in $map[$folder]) {
        $srcPath = Join-Path 'editor\src' $file
        $dstPath = Join-Path $dest $file
        if (Test-Path $srcPath) {
            Move-Item -Path $srcPath -Destination $dstPath -Force
            Write-Host "moved   $file  ->  editor/src/$folder/"
            $moved++
        } elseif (Test-Path $dstPath) {
            $already++
        } else {
            $missing += $file
        }
    }
}

Write-Host ""
Write-Host "Done. moved=$moved  already-in-place=$already"
if ($missing.Count -gt 0) { Write-Host ("NOT FOUND (check manually): " + ($missing -join ', ')) }
Write-Host "EditorApp_Camera.cpp is already in editor/src/app/ (created earlier)."
Write-Host "Next: clean-rebuild, then `git add -A` to record the renames."
