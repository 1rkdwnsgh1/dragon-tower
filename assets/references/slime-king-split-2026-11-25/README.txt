Dragon Tower | 슬라임 왕 분열 연출

파일
assets/enemies/slime-king/split.png — 2560×256, 10프레임, 350ms, 발 (128, 240), 오른쪽 지향
assets/enemies/slime-king/fx_split.png — 5120×512, 10프레임, 400ms, 중심 (256, 256)
assets/enemies/slime-king/fx_split_land.png — 2048×256, 8프레임, 350ms, 착지점 (128, 200)
각 시트는 투명 RGBA PNG이며 프레임은 왼쪽에서 오른쪽 순서다. 재생용 상세 값은 manifest.json에 있다.
split_preview.gif — 960×600, zone_01 위에 구현한 연출 예시. 작은 슬라임 발 위치는 왕 기준 110px 이동하고, 최고점은 이동 중 지면에서 70px 위다. 연출 시작 450ms 후 두 마리가 착지한다. GIF 끝의 잠깐 멈춤은 반복 확인용이다.

제작 도구
OpenAI 기본 이미지 생성 도구로 분열과 착지 점액의 피크 그림을 제작했다. Pillow/NumPy로 시트를 조립하고 프레임별 형태와 위치, 방울 이동, 감쇠를 조정했다.
왕은 GitHub main의 idle.png 1번 프레임을 발 기준으로 변형했다. 마지막 칸은 알파 기준을 맞춘 대기 1번 프레임이다. 작은 슬라임은 GitHub 원본과 같은 로컬 대기 시트로 GIF에 합성했다.
https://github.com/1rkdwnsgh1/dragon-tower/tree/main/assets/enemies

검수 및 범위
RGBA 모드, 시트 크기, 프레임 수, 왕의 발 위치, 효과 칸의 6% 안전 여백, 알파 1~16 제거를 확인했다.
점액 분열의 최대 본체 폭은 404px(512px의 약 79%), 착지 웅덩이 최대 폭은 160px이다. 분열 효과 마지막 칸은 완전히 사라진 상태를 나타내는 투명 프레임이다.
GIF의 효과 표시 배율은 분열 0.65배, 착지 0.64배로 설정했다. 원본 시트에는 이 표시 배율이 적용되지 않는다.
동작과 가독성은 합성 미리보기로 검수했다. 엔진에서의 실제 합성 위치와 스케일은 실행 검증하지 않았다.

이미지 생성 프롬프트
fx_split.png 피크 원본
Paint one isolated effect sprite key pose only, transparent background. No animation sheet. A symmetrical stylized wet GREEN viscous slime burst splashing out from a central low round body-free source, two big fat shapeless lobes directed to upper-left and upper-right, some attached pooling at their shared base, NO separate flying droplets (added during animation), bold compact V-shaped silhouette with short thick lobes and clear flat sides, no smoke, no glitter, no people or creature, no eyes, no ground, no shadows, no letters. Slime liquid is bright #5CE07A family with forest green edge, pale lime highlights. Handpainted fantasy action RPG matching the supplied Slime King reference material only; do not copy its silhouette, crown, cape or anatomy. Slightly elevated front angle. One effect on clean fully transparent background.

fx_split_land.png 피크 원본
Paint one isolated effect sprite key pose only, transparent background. A single flat wide fresh green viscous slime impact splat, a broad oval puddle seen slightly from above with a few short curled splash tips joined to the puddle, almost flat, width roughly 4 times height. No separate airborne droplets, no ground, shadows, no letters, no creature, no smoke or light rays. #5CE07A bright green main color, deep green rim, pale lime highlights. Match the handpainted goo material in the supplied slime boss reference only, omit silhouette, cape, crown or face. Bold broad flattened form readable at 60 px, true transparent exterior.

