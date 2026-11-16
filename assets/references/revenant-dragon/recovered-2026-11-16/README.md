# 망령룡 스킬 모션·이펙트 복구 자료

복구일: 2026-11-16. 원본 제작일: 2026-11-14.

사용자가 잘못 업로드한 저장소에서 찾아 원본 그대로 복사한 **V1 검토 자료**다.
게임에 연결하거나 기존 시트를 교체하지 않았다. 저장 파일·스킬 수치·개발 순서는 바뀌지 않는다.

## 출처와 사용 범위

- 원본: [kospher123-sys/dragon_](https://github.com/kospher123-sys/dragon_)
- 원본 커밋: `b96a77c16f9e525b6402373e82f61ccc04a33975`
- 이미지 원래 경로: `그림자드래곤/망령용/`
- 사용자의 이미지 제작 작업물이며 사용자 요청으로 이 저장소에 보관한다.
- 원본 안내에는 내장 `image_gen` 사용과 후처리가 명시되어 있다. 별도 공개 재배포 라이선스는 원본에 명시되어 있지 않으며 이번 복구로 새 라이선스를 부여하지 않는다.
- 원본 파일별 경로·바이트 수·SHA-256은 [복구 목록](recovery-manifest.json), 파일 검증 결과는 [verification.json](verification.json)에 있다.

## 6종 세트 — 본체와 효과 각각 보관

| 세트 | 본체 모션 | 분리형 이펙트 |
|---|---|---|
| 기본 공격: 망령탄·망자의 발톱 | [01_basic_attack_body](original/01_basic_attack_body/) | [02_basic_attack_effects](original/02_basic_attack_effects/) |
| 혼령 관통 | [03_spirit_pierce_body](original/03_spirit_pierce_body/) | [04_spirit_pierce_effects](original/04_spirit_pierce_effects/) |
| 영혼 수확 | [05_soul_harvest_body](original/05_soul_harvest_body/) | [06_soul_harvest_effects](original/06_soul_harvest_effects/) |
| 망령의 침식 | [07_spectral_erosion_body](original/07_spectral_erosion_body/) | [08_spectral_erosion_effects](original/08_spectral_erosion_effects/) |
| 망계의 숨결 | [09_underworld_breath_body](original/09_underworld_breath_body/) | [10_underworld_breath_effects](original/10_underworld_breath_effects/) |
| 죽음의 행렬 | [11_march_of_the_dead_body](original/11_march_of_the_dead_body/) | [12_march_of_the_dead_effects](original/12_march_of_the_dead_effects/) |

6종은 기본 공격의 원거리·근접을 한 세트로 센 수다. 개별 이름은 7개다.
원본 폴더의 프레임·시트·manifest·README·생성 프롬프트·검증 메모를 모두 보존했다.
358개 PNG와 JSON 12개를 포함하며 원본 세트 전체는 401개 파일이다.

## 관련 기획 메모

[concept-notes](concept-notes/)에는 원본 저장소의 추가 스킬 시각 콘셉트 문서 6개를 보존했다.
이 문서들의 **1~6차 콘셉트 묶음**은 위 **6종 모션·이펙트 세트**와 다르다.
문서에 적힌 추가 콘셉트 패널 이미지 원본은 해당 저장소에 없어서 이번 복구에 포함되지 않는다.
원본 문서 내부 `SPEC.md` 및 4슬롯 설명은 다른 저장소의 역사적 맥락이며 이 게임의 확정 규칙을 대체하지 않는다.
원본 링크는 [원본 SPEC.md](https://github.com/kospher123-sys/dragon_/blob/b96a77c16f9e525b6402373e82f61ccc04a33975/SPEC.md)에서 확인한다.

## 적용 전 확인

- 원본은 생성된 V1 초안이다. 색 테두리·잘림·형태 변화·정렬 오차와 레이어 합성을 검수해야 한다.
- 원본의 타이밍·이벤트 프레임은 제안값이다. 게임 판정이나 현재 모션 유지 시간에 바로 대입하지 않는다.
- 게임용 시트 변환은 [캐릭터 적용 기획](../../../../docs/character-pipeline.md)을 따른다.
- 기존 적용 모션과 바이트가 같은 파일도 원본 세트의 구성을 보존하기 위해 함께 두었다. 대응은 `verification.json`에서 확인한다.
- 이미지 손상·파일 누락 검증은 시각 검수·게임 내 재생 검증을 대신하지 않는다.
- 이 폴더는 작업용 원본이므로 실행 배포 묶음에는 포함하지 않는다.
