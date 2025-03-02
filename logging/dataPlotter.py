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

    start = 0
    end = 0

    if sys.argv[2] == "all":
        start = 1
        for ROWS in plotting:
            end = len(ROWS) - 1
            break
    else:
        start = int(sys.argv[2])
        end = start + 1 

    for i in range(start, end):
        datafile.seek(0)
        X = []
        Y = []

        for ROWS in plotting:
            X.append(ROWS[0])
            Y.append(ROWS[i])

        plt.title("BMW KCAN data")
        plt.figure().set_figwidth((len(X) * 20) / 4500)

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

        print("Plot successfully saved at " + plotFileName)

        plt.clf()
        plt.close()
