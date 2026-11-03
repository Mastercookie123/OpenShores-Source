HASH_PACK := build/linux-$(CONFIG)/bin/hashpack
STORY_COMPILER := build/linux-$(CONFIG)/bin/AuScript
SHADER_ARCHIVE := src/lib/graphics/resources/glsl.har
SHADER_SOURCES := $(wildcard src/lib/graphics/resources/shaders/*.glsl)

.PHONY: assets audio-assets shader-assets story-assets

# Client builds need runtime audio with either toolchain. Shader and story
# archives are checked in and rebuilt explicitly when their sources change.
linux linux-client linux-clients win win-client: audio-assets shader-assets

assets: audio-assets shader-assets story-assets

audio-assets:
	HAZERON_LINUX_IMAGE="$(LINUX_IMAGE)" \
	HAZERON_ASSETS_CACHE_VOLUME="$(ASSETS_CACHE_VOLUME)" \
		bash docker/linux/build-assets.sh

shader-assets: $(SHADER_ARCHIVE)

$(SHADER_ARCHIVE): $(SHADER_SOURCES)
	$(call run_linux_build,hashpack)
	cd src/lib/graphics/resources/shaders && \
		../../../../../$(HASH_PACK) -c ../glsl.har '*.glsl'

story-assets:
	$(call run_linux_build,AuScript)
	$(call run_linux_build,hashpack)
	python3 resources/stories/build-stories.py \
		--compiler "$(STORY_COMPILER)" \
		--hashpack "$(HASH_PACK)"
