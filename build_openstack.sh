#!/usr/bin/env bash


password_generator() {
openssl rand -hex 10
ADMIN_PASS
CINDER_DBPASS
CINDER_PASS
DASH_DBPASS
DEMO_PASS
GLANCE_DBPASS
GLANCE_PASS
KEYSTONE_DBPASS
METADATA_SECRET
NEUTRON_DBPASS
NEUTRON_PASS
NOVA_DBPASS
NOVA_PASS
PLACEMENT_PASS
RABBIT_PASS
}

apt_repository() {
echo "****** updating apt sources ******"
# $1 should be the openstack release
add-apt-repository cloud-archive:caracal
apt update -y
}

# ensure we're running via sudo
if [ $(whoami) = "root" ]
then
echo "executing as $(whoami)"

else
echo "error: must run \"sudo $0\""
exit
fi


# is this a raspberry pi 5?
if [ $(uname -r) = "6.11.0-1007-raspi" ]
        then
                echo "OS version match"
        else
                echo "OS version mismatch"
                exit
fi

echo "****** apt update ******"
apt-get update -y
echo "****** apt upgrade ******"
apt-get upgrade -y
echo "****** apt install ******"
#apt-get install -y ntpdate sudo vim htop tar iotop bridge-utils cpu-checker mysql-server ifupdown
apt-get install -y netplan.io etcd-server ntpdate mariadb-server python3-pymysql

echo "****** set ntp ******"
ntpdate time.nist.gov

#setup_sshd() {
#echo "****** setup_sshd() ******"
#echo "allow root login"
#sed -i -e 's/\#vnc_listen.*$/vnc_listen = "0.0.0.0"/g' /etc/libvirt/qemu.conf
#}

update_etcd_config() {
echo "****** update_etcd_config() ******"
echo "ETCD_UNSUPPORTED_ARCH=arm64" >> /etc/default/etcd
}



if [ $(grep "ERROR_FOR_DIVISION_BY_ZERO" /etc/mysql/mysql.conf.d/mysqld.cnf) ]
then
        echo "mysqld.cnf already updated"
else
        echo "updating mysqld.cnf"
        update_mysql_cnf
        update_mysqld_cnf
fi

echo "****** restart mysql ******"
systemctl restart mysql


# Setup Repo
#echo "****** setup apt repo ******"
#mkdir -p /etc/apt/keyrings
#wget -O- http://packages.shapeblue.com/release.asc | gpg --dearmor | sudo tee /etc/apt/keyrings/cloudstack.gpg > /dev/null
#echo deb [signed-by=/etc/apt/keyrings/cloudstack.gpg] http://packages.shapeblue.com/cloudstack/upstream/debian/4.20 / > /etc/apt/sources.list.d/cloudstack.list

# Install management server
echo "****** install openstack ******"
apt-get update
apt-get install -y 

# Stop the automatic start after install
#systemctl stop cloudstack-management cloudstack-usage

#echo "****** setup cloudstack database ******"
#setup_cloudstack_db

echo "****** kvm setup ******"
if [ $(grep "KVM_SETUP_SCRIPT" /etc/libvirt/qemu.conf) ]
then
        echo "kvm configs already updated"
else
        echo "updating kvm configs"
        update_kvm_configs
fi

#update_etc_group() {
#echo "updating /etc/group"
#groupadd wheel
#useradd -G wheel admin
#useradd -G wheel root
#}

update_kvm_configs() {
echo '#KVM_SETUP_SCRIPT"' >> /etc/libvirt/qemu.conf
sed -i -e 's/\#vnc_listen.*$/vnc_listen = "0.0.0.0"/g' /etc/libvirt/qemu.conf
echo 'security_driver = "none"' >> /etc/libvirt/qemu.conf
echo LIBVIRTD_ARGS=\"--listen\" >> /etc/default/libvirtd
echo 'listen_tls=0' >> /etc/libvirt/libvirtd.conf
echo 'listen_tcp=1' >> /etc/libvirt/libvirtd.conf
echo 'tcp_port = "16509"' >> /etc/libvirt/libvirtd.conf
echo 'tls_port = "16514"' >> /etc/libvirt/libvirtd.conf
echo 'mdns_adv = 0' >> /etc/libvirt/libvirtd.conf
echo 'auth_tcp = "none"' >> /etc/libvirt/libvirtd.conf
}

mariadb_config() {
if [ -f /etc/mysql/mariadb.conf.d/99-openstack.cnf ]
then
echo "****** mariadb conf ******"
cat <<EOF > /etc/mysql/mariadb.conf.d/99-openstack.cnf
[mysqld]
bind-address = 192.168.0.154

default-storage-engine = innodb
innodb_file_per_table = on
max_connections = 4096
collation-server = utf8_general_ci
character-set-server = utf8
EOF

else
echo "****** mariadb already configured ******"
fi
}



echo "****** configure iptables ******"
echo "doing nothing"

echo "****** mysql defaults ******"
cat /etc/mysql/debian.cnf

#echo "****** mask libvirt ******"
#systemctl mask libvirtd.socket libvirtd-ro.socket libvirtd-admin.socket libvirtd-tls.socket libvirtd-tcp.socket

if [ $(grep "EDITOR" /root/.bashrc) ]
then
echo "export EDITOR=/usr/bin/vi" >> /root/.bashrc
else :
fi

update_etc_group
apt_repository
#install_apt
mariadb_config
#install_message_queue
#install_memcached
#install_etcd

# exit sudo
exit


#echo "****** start cloudstack ******"
#cloudstack-setup-management
#systemctl status cloudstack-management
#tail -f /var/log/cloudstack/management/management-server.log

