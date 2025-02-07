#!/usr/bin/env bash

# TODO: change amd64 to arm64  /usr/share/cloudstack-common/scripts/util/create-kubernetes-binaries-iso.sh
# TODO: upload iso via api
# http://localhost:8080/client/api?command=deployVirtualMachine&serviceOfferingId=1&diskOfferingId=1&templateId=2&zoneId=4&apiKey=miVr6X7u6bN_sdahOBpjNejPgEsT35eXq-jB8CG20YI3yaxXcgpyuaIRmFI_EJTVwZ0nUkkJbPmY3y2bciKwFQ&signature=Lxx1DM40AjcXU%2FcaiK8RAP0O1hU%3D
#
# https://cloudstack.apache.org/api/apidocs-4.16/apis/addKubernetesSupportedVersion.html
#
# curl -X POST http://localhost:8080/client/api?command=addKubernetesSupportedVersion





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
