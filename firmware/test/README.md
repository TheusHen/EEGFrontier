# PlatformIO Tests

## Run unit tests (host / local PC)

```bash
pio test -e native
```

## Build firmware (RP2040 / XIAO)

```bash
pio run -e xiao_rp2040
```

## Optional: upload firmware

```bash
pio run -e xiao_rp2040 -t upload
```
