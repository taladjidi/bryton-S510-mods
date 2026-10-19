"""tqdm if installed, otherwise no progress bar (it is only cosmetic)."""
try:
    from tqdm import tqdm
except ImportError:
    class _NoBar:
        def __enter__(self): return self
        def __exit__(self, *a): pass
        def update(self, n): pass

    def tqdm(iterable=None, **_):
        return _NoBar() if iterable is None else iterable
