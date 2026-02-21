import matplotlib.pyplot as plt
import numpy as np
import sys
import os

if len(sys.argv) != 2:
    print(f'Received {len(sys.argv) -1} arguments, expected usage: python showHistogram histogram.txt \n')
    exit()

histogram = np.loadtxt(sys.argv[1], dtype = int, delimiter = ',')
plt.bar(histogram[:,0], histogram[:,1], width=1.0, edgecolor='black')
plt.ylabel('Pixel count')
plt.xlabel('Intensity value')
plt.show()

#wsl correction
output_file = os.path.splitext(sys.argv[1])[0] + '.png'
plt.savefig(output_file)
print(f"Histogram saved to {output_file}")
