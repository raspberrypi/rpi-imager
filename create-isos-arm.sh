#!/usr/bin/env bash

urls="
https://dl.k8s.io/v1.32.1/bin/linux/arm64/apiextensions-apiserver
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kube-aggregator
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kube-apiserver
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kube-controller-manager
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kube-log-runner
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kube-proxy
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kube-scheduler
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kubeadm
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kubectl
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kubectl-convert
https://dl.k8s.io/v1.32.1/bin/linux/arm64/kubelet
https://dl.k8s.io/v1.32.1/bin/linux/arm64/mounter
"

for url in $(echo ${urls})
do
	echo "url: $url"
done
