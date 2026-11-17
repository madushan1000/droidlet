#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

${SCRIPT_DIR}/apply-patches.sh
${SCRIPT_DIR}/link-aosp-meson.sh
