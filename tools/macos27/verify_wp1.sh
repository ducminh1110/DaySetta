#!/usr/bin/env bash
# WP1b verifier: bash + jq. Last line: "VERIFY PASS" or "VERIFY FAIL: <reason>".
cd "$(dirname "$0")/../.." || exit 1
J=artifacts/macos-27/mig-routines.json
fail() { echo "VERIFY FAIL: $1"; exit 1; }
jq -e . "$J" >/dev/null 2>&1 || fail "invalid or missing $J"
jq -e '.schema_version and .source.cache and .source.build=="26A434" and .source.skylight_uuid
  and .source.text_start and .source.text_end and (.subsystems|type=="array") and (.diff_vs_26_6|type=="object")' "$J" >/dev/null \
  || fail "missing required keys"
B=$(jq -r .source.baseline "$J"); [ -f "$B" ] || fail "baseline $B missing"
live=$(python3 -I tools/macos27/scan_mig.py --text-bounds) || fail "--text-bounds failed"
mine="$(jq -r '.source.text_start+" "+.source.text_end' "$J")"
[ "$live" = "$mine" ] || fail "text bounds differ: JSON '$mine' vs live '$live'"
jq -e --slurpfile b "$B" '
  def n: if type=="string" then (.[2:]|ascii_downcase) as $h | reduce ($h|explode[]) as $c (0; .*16 + (if $c>=97 then $c-87 else $c-48 end)) else . end;
  (.source.text_start|n) as $t0 | (.source.text_end|n) as $t1
  | def intext: if . == "0x0" or . == null then true else ((n) as $v | $v>=$t0 and $v<$t1) end;
  ($b[0] | group_by(.subsystem) | map({key:.[0].subsystem, value:{start:(map(.id)|min), r:(map({key:(.id|tostring), value:.routine})|from_entries)}}) | from_entries) as $base
  | all(.subsystems[];
      . as $s
      | ($s.server_vmaddr|intext)
      and ($s.start < $s.end) and ($s.routine_count == ($s.routines|length))
      and (($s.name=="?") or ($s.name_status|contains("inferred")))
      and (($s.name=="?") or ($base[$s.name].start == $s.start))
      and all($s.routines[];
          (.id >= $s.start and .id < $s.end)
          and (.stub_vmaddr|intext) and (.stub_vmaddr != "0x0") and (.impl_vmaddr|intext)
          and ((.name=="?") or ((.name_status|contains("inferred")) and $s.name!="?" and ($base[$s.name].r[(.id|tostring)] == .name)))))
' "$J" >/dev/null || fail "subsystem/routine consistency, 26.6 name match, or __TEXT bounds check"
echo "subsystems: $(jq '.subsystems|length' "$J"), routines: $(jq '[.subsystems[].routine_count]|add' "$J")"
echo "VERIFY PASS"
