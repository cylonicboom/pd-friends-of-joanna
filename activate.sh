# Friends of Joanna checkout activation. Idempotent; safe to re-source.
# Usage from a pd-fojo checkout: source ./activate.sh

_fojo_pd_src="${BASH_SOURCE[0]:-${(%):-%N}}"
_fojo_pd_root="$(cd "$(dirname "$_fojo_pd_src")" && pwd)"
unset _fojo_pd_src

_FOJO_PD_OLD_PD_ROMFILE="${PD_ROMFILE:-}"
_FOJO_PD_OLD_FOJO_ROOT="${FOJO_ROOT:-}"

if [ -f "$_fojo_pd_root/tools/activate.sh" ]; then
    . "$_fojo_pd_root/tools/activate.sh"
else
    echo "fojo activation failed: $_fojo_pd_root/tools/activate.sh not found" >&2
    unset _fojo_pd_root _FOJO_PD_OLD_PD_ROMFILE _FOJO_PD_OLD_FOJO_ROOT
    return 1 2>/dev/null || exit 1
fi

export PD="$_fojo_pd_root"

_fojo_pd_basedir_rom="$_fojo_pd_root/basedir/data/pd.ntsc-final.z64"
if [ -z "${PD_ROMFILE:-}" ] && [ -f "$_fojo_pd_basedir_rom" ]; then
    export PD_ROMFILE="$(cd "$(dirname "$_fojo_pd_basedir_rom")" && pwd)/$(basename "$_fojo_pd_basedir_rom")"
fi
unset _fojo_pd_basedir_rom

deactivate_fojo() {
    deactivate_fojo_tools
    if [ -n "$_FOJO_PD_OLD_PD_ROMFILE" ]; then export PD_ROMFILE="$_FOJO_PD_OLD_PD_ROMFILE"; else unset PD_ROMFILE; fi
    if [ -n "$_FOJO_PD_OLD_FOJO_ROOT" ]; then export FOJO_ROOT="$_FOJO_PD_OLD_FOJO_ROOT"; else unset FOJO_ROOT; fi
    unset _FOJO_PD_OLD_PD_ROMFILE _FOJO_PD_OLD_FOJO_ROOT
    unset -f deactivate_fojo
}

unset _fojo_pd_root
echo "fojo checkout active"
echo "  PD          = $PD"
if [ -n "${PD_ROMFILE:-}" ]; then
    echo "  PD_ROMFILE  = $PD_ROMFILE"
fi
echo "run  deactivate_fojo  to restore"