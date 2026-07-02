#!/bin/sh

if [ -n "${SCRIPT_DIR:-}" ] && [ -f "$SCRIPT_DIR/env.sh" ]; then
    SCRIPT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR" && pwd)
elif [ -f "./scripts/env.sh" ]; then
    SCRIPT_DIR=$(CDPATH= cd -- "./scripts" && pwd)
else
    SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fi
PROJ_DIR=$(dirname "$SCRIPT_DIR")
SDK_MPP_DIR=$(CDPATH= cd -- "$PROJ_DIR/.." && pwd)

add_ld_path()
{
    if [ -d "$1" ]; then
        case ":${LD_LIBRARY_PATH:-}:" in
            *":$1:"*) ;;
            *) LD_LIBRARY_PATH="$1${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
        esac
    fi
}

add_path()
{
    if [ -d "$1" ]; then
        case ":${PATH:-}:" in
            *":$1:"*) ;;
            *) PATH="$1${PATH:+:$PATH}" ;;
        esac
    fi
}

add_ld_path "$PROJ_DIR/out/lib"
add_ld_path "$PROJ_DIR/out/lib/svp_npu"
add_ld_path "$SDK_MPP_DIR/out/lib"
add_ld_path "$SDK_MPP_DIR/out/lib/svp_npu"
add_ld_path "$SDK_MPP_DIR/out/lib/extdrv"

add_path "$PROJ_DIR/out/bin"

export LD_LIBRARY_PATH
export PATH
