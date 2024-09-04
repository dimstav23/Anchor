Example of running ANCHOR indices with ASan:

source dir : `/path/to/Anchor/`
install dir : `/path/to/Anchor/anchor_sanitizer/build`

```
mkdir source_dir/anchor_sanitizer
make clean && make clean ANCHOR_FUNCS=1
make ANCHOR_FUNCS=1 ENCR_OFF=0 SANITIZE=address
make install ANCHOR_FUNCS=1 ENCR_OFF=0 SANITIZE=address prefix=/path/to/Anchor/anchor_sanitizer
LD_LIBRARY_PATH=/path/to/Anchor/anchor_sanitizer/lib ./pmembench anchor_pmembench_map_sanitizer.cfg
```
