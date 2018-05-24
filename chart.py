#!/usr/bin/env python3

import sys
import json
from pprint import pprint
import re
import math
import numpy as np
import scipy.stats as st
from matplotlib import rcParams
rcParams["backend"] = "Agg"
from matplotlib.backends.backend_pdf import PdfPages

import matplotlib.pyplot as plt
from matplotlib.patches import Patch

def mean_interval(confidence, data):
	mean = np.mean(data)
	if len(data) <= 1: return (mean, 0)
	standard_error = st.sem(data)
	if standard_error == 0: return (mean, 0)
	low, high = st.t.interval(confidence, len(data) - 1, loc=mean, scale=standard_error)
	assert(math.isclose(mean - low, high - mean))
	return (mean, mean - low)

def main(inputs):
	data = {}
	for input in inputs:
		with open(input) as f:
			raw_data = json.load(f)
		for b in raw_data["benchmarks"]:
			match = re.fullmatch("(?P<name>[a-zA-Z_]+)<(?P<template>[^>]+)>/(?P<size>\\d+)(?:_(?P<stat>mean|median|stddev))?", b["name"])
			if not match:
				print("Borked match on name", b["name"])
				sys.exit(1)
			if match.group("stat"):
				continue # it would be much easier if it was possible to disable this....
			if match.group("name") not in data:
				data[match.group("name")] = {}
			if match.group("template") not in data[match.group("name")]:
				data[match.group("name")][match.group("template")] = {}
			if int(match.group("size")) not in data[match.group("name")][match.group("template")]:
				data[match.group("name")][match.group("template")][int(match.group("size"))] = []
			data[match.group("name")][match.group("template")][int(match.group("size"))].append(b)

	with PdfPages('graphs.pdf') as pdf:
		for name,group in sorted(data.items()):
			plt.figure(figsize=[11.69, 8.27])
			plt.title(f"{name} (99% confidence)")
			plt.yscale('log')
			plt.xscale('log')
			plt.grid(True)
			for template,series in sorted(group.items()):
				ser = sorted(series.items())
				values = [mean_interval(0.99, [y["real_time"] for y in x[1]]) for x in ser]
				plt.errorbar([t[0] for t in ser], [t[0] for t in values], yerr=[t[1] for t in values], label=f"INITIAL_SIZE={template}")

			plt.legend()
			pdf.savefig()
			plt.close()

import argparse
parser = argparse.ArgumentParser()
parser.add_argument("inputs", nargs="+")
args = parser.parse_args()
main(args.inputs)
