import matplotlib.pyplot as plt
import csv
import sys

if len(sys.argv) != 3:
    print("Illegal argument length!")
    print("Usage: 'python dataPlotter.py [log_converted.csv] [column to plot]'")
    print("Example: 'python dataPlotter.py testData_converted.csv 18'")
    exit()
 
X = []
Y = []
 
with open(sys.argv[1], 'r') as datafile:
    plotting = csv.reader(datafile, delimiter=';')
     
    for ROWS in plotting:
        X.append(ROWS[0])
        Y.append(ROWS[int(sys.argv[2])])

plt.title("BMW KCAN data")

xLabel = X.pop(0)
yLabel = Y.pop(0)

plt.xlabel(xLabel)
plt.ylabel(yLabel)

for i in range(len(X)):
    X[i] = float(X[i])

for i in range(len(Y)):
    Y[i] = float(Y[i])

plt.plot(X,Y)

plotFileName = sys.argv[1].replace(".csv", "_" + yLabel + ".pdf")

plt.savefig(plotFileName, bbox_inches='tight')

print("Plot succelfully saved at " + plotFileName)
