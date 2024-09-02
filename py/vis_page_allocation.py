import matplotlib.pyplot as plt
import numpy as np
import glob
import seaborn as sns

import multiprocessing
from joblib import Parallel, delayed
from tqdm import tqdm
import pickle
import os

scatter = True
heatmap = True

def count_range_in_list(li, min, max):
    ctr = 0
    for x in li:
        if min <= x <= max:
            ctr += 1
    return ctr

files = glob.glob('mem*.txt', recursive=True)
fig, ax = plt.subplots(2, max(len(files), 2))

for n, fn in enumerate(files):
    bits = [ch == '1' for ch in open(fn).read()]

    print(fn)

    addresses = [i for i, b in enumerate(bits) if not b]

    ### scatterplot (+ jitter)

    if scatter:
        print("  scatter")

        ax[0, n].scatter(addresses, np.random.randn(len(addresses)), marker='.')
        ax[0, n].title.set_text(fn)
        ax[0, n].set_yticks([])

    # heatmap

    if heatmap:
        print("  heatmap")
        
        r = 10000
        pn = f"hm_{fn}"
        if not os.path.exists(pn):
            print("    downsampling")

            num_cores = multiprocessing.cpu_count()
            y = Parallel(n_jobs=num_cores)(delayed(count_range_in_list)(addresses, x, x + r)
                                            for x in tqdm(range(addresses[0], addresses[-1] + 1, r)))

            with open(pn, "wb") as pf:
                pickle.dump(y, pf)
        else:
            with open(pn, "rb") as pf:
                y = pickle.load(pf)

        image = np.array(y)
        image = image.reshape(1, len(image))
        sns.heatmap(image, vmin=0, vmax=r, cmap="rocket_r",
                    cbar=False, linewidths=0.0, ax=ax[1, n], yticklabels=False)

plt.tight_layout()
plt.show()
