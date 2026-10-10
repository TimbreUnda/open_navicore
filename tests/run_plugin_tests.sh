#!/bin/sh
set -eu

cc=${CC:-cc}
readelf_bin=${READELF:-readelf}
build_dir=${1:?usage: run_plugin_tests.sh build-dir}
mkdir -p "$build_dir/plugins"

common_flags="-std=c99 -O2 -g -Wall -Wextra -Werror -pthread -fPIC -fvisibility=hidden -DMOCK_REF_PLUGIN_HEADLESS -Iinclude -Isrc/compat/qr39 -Isrc/runtime -Isrc/backend -Isrc/renderer -Isrc/capture ${EXTRA_CFLAGS:-}"
plugin_path="$build_dir/plugins/active.so"
trace_path="$build_dir/reference-plugin.log"

build_plugin() {
    output=$1
    failure=$2
    invalid=$3
    $cc $common_flags -shared \
        -DMOCK_REF_PLUGIN_FAILURE="$failure" \
        -DMOCK_REF_PLUGIN_INVALID="$invalid" \
        -DMOCK_REF_PLUGIN_TRACE_PATH='"'"$trace_path"'"' \
        -Wl,--version-script=backends/reference/reference_backend.map \
        -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now \
        backends/reference/reference_backend.c -o "$output"
}

build_plugin "$build_dir/plugins/success.so" 0 0
$cc $common_flags -shared \
    -DMOCK_REF_PLUGIN_FAILURE=0 -DMOCK_REF_PLUGIN_INVALID=0 \
    -DMOCK_REF_PLUGIN_MAX_QUERY_ABI=2 \
    -DMOCK_REF_PLUGIN_TRACE_PATH='"'"$trace_path"'"' \
    -Wl,--version-script=backends/reference/reference_backend.map \
    -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now \
    backends/reference/reference_backend.c -o "$build_dir/plugins/query-v2-only.so"
$cc $common_flags -shared \
    -DMOCK_REF_PLUGIN_FAILURE=0 -DMOCK_REF_PLUGIN_INVALID=0 \
    -DMOCK_REF_PLUGIN_MAX_QUERY_ABI=3 \
    -DMOCK_REF_PLUGIN_TRACE_PATH='"'"$trace_path"'"' \
    -Wl,--version-script=backends/reference/reference_backend.map \
    -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now \
    backends/reference/reference_backend.c -o "$build_dir/plugins/query-v3-only.so"
$cc $common_flags -shared \
    -DMOCK_REF_PLUGIN_FAILURE=0 -DMOCK_REF_PLUGIN_INVALID=0 \
    -DMOCK_REF_PLUGIN_QUERY_V1_ONLY=1 \
    -DMOCK_REF_PLUGIN_TRACE_PATH='"'"$trace_path"'"' \
    -Wl,--version-script=backends/reference/reference_backend.map \
    -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now \
    backends/reference/reference_backend.c -o "$build_dir/plugins/query-v1-only.so"
$cc $common_flags -shared \
    -DMOCK_REF_PLUGIN_FAILURE=0 -DMOCK_REF_PLUGIN_INVALID=0 \
    -DMOCK_REF_PLUGIN_OUTBOUND_TEST=1 \
    -DMOCK_REF_PLUGIN_TRACE_PATH='"'"$trace_path"'"' \
    -Wl,--version-script=backends/reference/reference_backend.map \
    -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now \
    backends/reference/reference_backend.c -o "$build_dir/plugins/outbound.so"
build_plugin "$build_dir/plugins/start-fail.so" 1 0
build_plugin "$build_dir/plugins/surface-fail.so" 2 0
build_plugin "$build_dir/plugins/render-fail.so" 3 0
build_plugin "$build_dir/plugins/bad-capabilities.so" 0 1
build_plugin "$build_dir/plugins/bad-callback.so" 0 2
build_plugin "$build_dir/plugins/bad-pointer.so" 0 3
build_plugin "$build_dir/plugins/bad-build-id.so" 0 4
build_plugin "$build_dir/plugins/query-fail.so" 0 5

$cc $common_flags -shared -Wl,-z,now \
    tests/fixtures/plugins/no_query_backend.c -o "$build_dir/plugins/no-query.so"

$cc $common_flags -shared -Wl,-soname,libmocknav-missing.so -Wl,-z,defs \
    tests/fixtures/plugins/missing_dependency.c \
    -o "$build_dir/plugins/libmocknav-missing.so"
$cc $common_flags -shared -Wl,-z,now -Wl,-z,defs -Wl,--no-as-needed \
    tests/fixtures/plugins/unresolved_dependency_backend.c \
    -L"$build_dir/plugins" -lmocknav-missing \
    -o "$build_dir/plugins/missing-needed.so"
needed=$($readelf_bin -d "$build_dir/plugins/missing-needed.so" | \
    sed -n 's/.*NEEDED.*\[\([^]]*\)\].*/\1/p')
printf '%s\n' "$needed" | grep -Fxq libmocknav-missing.so || {
    echo "missing-dependency fixture lacks libmocknav-missing.so DT_NEEDED" >&2
    exit 1
}
rm -f "$build_dir/plugins/libmocknav-missing.so"

# This object has no libc dependency, so generating a foreign ELF class does
# not require host multilib runtime packages. The host linker must support the
# standard i386 emulation for this mandatory negative test.
$cc -m32 -shared -nostdlib -fPIC -fvisibility=hidden \
    tests/fixtures/plugins/wrong_class_backend.c \
    -o "$build_dir/plugins/wrong-class.so"

$cc $common_flags \
    -DMOCK_NAVICORE_DISABLE_LOG \
    -DMOCK_NAVICORE_TESTING \
    -DMOCK_NAVICORE_RENDERER_STUB \
    -DMOCK_NAV_BACKEND_PLUGIN_PATH='"'"$plugin_path"'"' \
    -DEXPECT_TRACE_PATH='"'"$trace_path"'"' \
    src/compat/qr39/navicore_compat.c src/runtime/mock_api_telemetry.c src/runtime/mock_callback_backend.c \
    src/runtime/mock_cluster_backend.c src/backend/mock_backend_loader.c \
    src/compat/qr39/host_callbacks.c src/runtime/mock_gps_backend.c \
    src/runtime/mock_pointer_backend.c src/runtime/mock_scalar_backend.c \
    src/renderer/mock_renderer.c src/renderer/mock_screen_input.c src/backend/mock_default_backend.c \
    src/platform/host/mock_renderer_stub.c tests/plugin_lifecycle_smoke.c \
    -ldl -o "$build_dir/plugin-lifecycle-smoke"

$cc $common_flags -DMOCK_NAVICORE_TESTING -DMOCK_NAVICORE_RENDERER_STUB \
    src/backend/mock_backend_loader.c src/renderer/mock_renderer.c src/renderer/mock_screen_input.c \
    src/backend/mock_default_backend.c src/platform/host/mock_renderer_stub.c \
    tests/plugin_handshake_smoke.c -ldl \
    -o "$build_dir/plugin-handshake-smoke"
"$build_dir/plugin-handshake-smoke"

run_case() {
    source_file=$1
    reason=$2
    valid=$3
    cycles=$4
    start_fail=$5
    surface_fail=$6
    render_fail=$7
    outbound=${8:-0}
    query_abi=${9:-4}
    if [ "$source_file" = missing ]; then
        rm -f "$plugin_path"
    else
        cp "$source_file" "$plugin_path"
    fi
    "$build_dir/plugin-lifecycle-smoke" "$reason" "$valid" "$cycles" \
        "$start_fail" "$surface_fail" "$render_fail" "$outbound" \
        "$query_abi"
}

run_case "$build_dir/plugins/success.so" 0 1 2 0 0 0
run_case "$build_dir/plugins/query-v3-only.so" 0 1 2 0 0 0 0 3
run_case "$build_dir/plugins/query-v2-only.so" 0 1 2 0 0 0 0 2
run_case "$build_dir/plugins/query-v1-only.so" 0 1 2 0 0 0 0 1
run_case "$build_dir/plugins/outbound.so" 0 1 2 0 0 0 1
run_case "$build_dir/plugins/start-fail.so" 6 1 1 1 0 0
run_case "$build_dir/plugins/surface-fail.so" 7 1 1 0 1 0
run_case "$build_dir/plugins/render-fail.so" 8 1 1 0 0 1
run_case "$build_dir/plugins/bad-capabilities.so" 4 0 1 0 0 0
run_case "$build_dir/plugins/bad-callback.so" 4 0 1 0 0 0
run_case "$build_dir/plugins/bad-pointer.so" 4 0 1 0 0 0
run_case "$build_dir/plugins/bad-build-id.so" 4 0 1 0 0 0
run_case "$build_dir/plugins/query-fail.so" 3 0 1 0 0 0
run_case "$build_dir/plugins/no-query.so" 2 0 1 0 0 0
run_case "$build_dir/plugins/missing-needed.so" 1 0 1 0 0 0
run_case "$build_dir/plugins/wrong-class.so" 1 0 1 0 0 0
run_case missing 1 0 1 0 0 0

echo "reference plugin host tests passed"
