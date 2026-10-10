#!/bin/sh
set -eu

expected_hash=6bc343da57552df88c509b01d839524eb5b119a610b19a9bb8f6d84f2a71e586
base=${FIRMWARE_UPDATES_DIR:?set FIRMWARE_UPDATES_DIR to the firmware archive directory}
qr395=$base/USA-ASO-8.4-QR-39.5.0-20210201-Q0H0/unpacked/encrypted_app/extracted/pas/scfg/ns_mq.cfg
qr396=$base/USA-ASO-8.4-QR-39.6.0-20210405-Q0H4/unpacked/encrypted_app/extracted/pas/scfg/ns_mq.cfg
sm395=$base/USA-ASO-8.4-QR-39.5.0-20210201-Q0H0/unpacked/encrypted_flash/extracted/seg4/pas/scfg/sm_config_launch.cfg
sm396=$base/USA-ASO-8.4-QR-39.6.0-20210405-Q0H4/unpacked/encrypted_flash/extracted/seg4/pas/scfg/sm_config_launch.cfg

for config in "$qr395" "$qr396"; do
    test "$(sha256sum "$config" | awk '{print $1}')" = "$expected_hash"
    grep -q '^ss_service_lifecycle_test_client=64' "$config"
    ! grep -q '^sm_reload_probe=' "$config"
    ! grep -q '^ss_plm_non_prod_cmd=' "$config"
done

for launch_config in "$sm395" "$sm396"; do
    ! grep -q '| ss_service_lifecycle_test_client ' "$launch_config"
done

test ! -e "$base/USA-ASO-8.4-QR-39.5.0-20210201-Q0H0/unpacked/encrypted_flash/extracted/seg7/pas/bin/ss_service_lifecycle_test_client"
test ! -e "$base/USA-ASO-8.4-QR-39.6.0-20210405-Q0H4/unpacked/encrypted_flash/extracted/seg7/pas/bin/ss_service_lifecycle_test_client"
grep -q 'std::string service_name("ss_service_lifecycle_test_client")' sm_reload_probe.cpp
