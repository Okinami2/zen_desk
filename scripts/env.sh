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
add_ld_path "/opt/lib"

add_path "$PROJ_DIR/out/bin"

if [ -d "/opt/plugins" ]; then
    QT_QPA_PLUGIN_PATH="/opt/plugins${QT_QPA_PLUGIN_PATH:+:$QT_QPA_PLUGIN_PATH}"
    export QT_QPA_PLUGIN_PATH
fi

export LD_LIBRARY_PATH
export PATH


case "$(basename -- "$0" 2>/dev/null)" in
    env.sh)
        echo "[INFO] env.sh was executed in a child shell." >&2
        echo "[INFO] To affect the current shell, run: . ./scripts/env.sh" >&2
        echo "[INFO] Current LD_LIBRARY_PATH prepared for child shell:" >&2
        echo "$LD_LIBRARY_PATH" >&2
        ;;
esac
