# Build

A raw make command will by default make the server in release mode. This mode
does not print messages about each request to console. This keeps control of the
CPU more because there are less systemcalls? Also -O3.

```bash
make
```

If you would like to see what processes are handling what request use
```bash
make debug
```
