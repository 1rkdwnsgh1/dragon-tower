# 용광 융단폭격 V1.1

`preview/v1_1_fly_composite.gif`는 승인 FLY와 6발 폭격을 합성한 미리보기다. `preview/v1_vs_v1_1_sync.gif`는 같은 시간축을 나란히 표시하고, `preview/bombardment_6shots.apng`는 FX 단독 합성이다. `preview/impact_fx_only.gif`에서 새 충격 FX만 볼 수 있다.

- V1 포탄·점화·폭발·연기 PNG 및 시트와 원본 마스터는 그대로 보관했다. 각 FX는 독립 재생한다.
- 착탄 이벤트에서 고열 섬광과 지면 충격파를 동일 앵커에 실행한다. Preview의 폭발은 37ms, 잔류 연기는 258ms 뒤에 시작한다. 이 수치는 화면 검토용이며 피해 판정은 별개다.
- 포탄의 오른쪽 탄두 피벗을 월드 위치에 붙이고 이동 접선 방향으로 회전한다. 발사구와 충돌 접점은 실제 게임에서 연결·검증한다.
- `metadata.json`은 시트 규격과 6발 예시 타임라인, `qa/`는 SHA-256 및 AssetQA 경고를 담고 있다.
