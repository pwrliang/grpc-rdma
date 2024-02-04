import matplotlib.pylab as pylab

params = {'legend.fontsize': 'medium',
          'axes.labelsize': 'large',
          'axes.titlesize': 'large',
          'xtick.labelsize': 'medium',
          'ytick.labelsize': 'medium',
          'hatch.linewidth': '0.15'}
pylab.rcParams.update(params)

print("Settings are imported")

light_colors = ['#6C87EA', '#FF3333', '#FFDF33', '#33FFFF', 'lightgreen']
patterns = ['', '\\\\', '\\\\--', '..', '..--']
markers = ['', "o", '^', 's', '*', 'x']
