param(
    [Parameter(Mandatory = $true)]
    [string]$Message
)

$ErrorActionPreference = "Stop"

$env:GIT_AUTHOR_NAME = "Kexodus"
$env:GIT_AUTHOR_EMAIL = "120147028+Kexodus@users.noreply.github.com"
$env:GIT_COMMITTER_NAME = "Kexodus"
$env:GIT_COMMITTER_EMAIL = "120147028+Kexodus@users.noreply.github.com"

try {
    git commit -m $Message
}
finally {
    Remove-Item Env:\GIT_AUTHOR_NAME -ErrorAction SilentlyContinue
    Remove-Item Env:\GIT_AUTHOR_EMAIL -ErrorAction SilentlyContinue
    Remove-Item Env:\GIT_COMMITTER_NAME -ErrorAction SilentlyContinue
    Remove-Item Env:\GIT_COMMITTER_EMAIL -ErrorAction SilentlyContinue
}
