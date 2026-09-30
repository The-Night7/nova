# Nova — compilation
#   make          version optimisée
#   make debug    avec AddressSanitizer/UBSan (détecte les erreurs mémoire)
#   make test     exécute les exemples, y compris sous NOVA_GC_STRESS=1

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
SRC     := $(wildcard src/*.c)
HDR     := $(wildcard src/*.h)

nova: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

debug: CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer
debug: clean nova

test: nova
	@for f in exemples/*.nv; do \
		printf '%-32s' "$$f"; \
		./nova --no-color $$f > /dev/null 2>&1; a=$$?; \
		NOVA_GC_STRESS=1 ./nova --no-color $$f > /dev/null 2>&1; b=$$?; \
		echo "code $$a (GC stress : $$b)"; \
	done

clean:
	rm -f nova

.PHONY: debug test clean
