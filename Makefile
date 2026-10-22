# Compatibility aliases: keep one supported build and never delete user data.
ifeq ($(OS),Windows_NT)
RUN = powershell.exe -NoProfile -File ./run-wsl.ps1 -Action
else
RUN = bash scripts/run-linux.sh
endif

.PHONY: all build clean test demo gui
all: build
build:
	$(RUN) build
test:
	$(RUN) test
demo:
	$(RUN) demo
gui:
	$(RUN) run
clean:
	$(RUN) clean
