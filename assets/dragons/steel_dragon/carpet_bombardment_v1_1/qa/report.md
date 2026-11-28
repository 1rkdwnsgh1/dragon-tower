# Skill 05 V1.1 검수

- V1 파일 46개 PNG를 에셋 및 백업과 SHA-256 대조했다. 여기에는 38개 프레임, 시트 4개, 오리지널 마스터 4개가 포함된다. FLY SHA는 추진 V2 정렬 기준과 일치한다.
- 새 섬광 4F와 지면 충격파 6F는 24FPS, RGBA8이다. 모든 픽셀은 셀 안에 있고 투명 영역에는 RGB가 남지 않는다. 착탄 전 새 FX가 재생되지 않는다.
- 충격파의 알파 1 이상 가시 폭은 [113, 186, 265, 329, 382, 421]px이다. 후반은 에너지가 약해지며 양끝 압력선과 작은 잔광만 남는다.
- 초기안은 종료에 주황색 띠가 남아 소멸을 다시 설계했다. 최종 마지막 프레임의 알파 16 이상 면적은 피크 대비 섬광 0.1%, 파동 0.0%다.
- 발사 간격은 [0.14, 0.44, 0.16, 0.43, 0.15]초다. 각 탄두의 궤적 종점과 착탄 앵커 오차는 0.0px다. 포탄 각도는 이동 접선에서 구하며 30FPS 프레임 사이 최대 변화는 5.40도였다.
- 여섯 위치에서 섬광, 충격파, 폭발, 연기가 모두 독립 재생됐다. 연쇄 폭발 구간에 폭발 인스턴스가 0인 프레임은 없었고 최대 3개가 겹쳤다.
- AssetQA 오류 0건. 기존 경고 4건은 V1과 정확히 같았다. 신규 경고 0건은 원본 경고와 따로 기록했다. 신규 집계 {'impact_flash': {'FAIL': {}, 'WARN': {}, 'REVIEW': {'game_readability': 1, 'visual_timing': 1, 'visual_alignment': 1}}, 'ground_shockwave': {'FAIL': {}, 'WARN': {}, 'REVIEW': {'game_readability': 1, 'visual_timing': 1, 'visual_alignment': 1}}}.
- 사용 원본 및 확인 범위. attachment transfer DNS failed; use published V1 with matching local/Downloads SHA-256 and identical attachment length.
- 게임 크기는 102px FLY 셀로 만든 1× 비교 Preview다. 실제 엔진의 포격구 위치, 낙하 판정, 피해 판정은 아직 확인하지 않았다.
