#!/usr/bin/env bash

setup_cloudstack_db() {
	# from https://docs.cloudstack.apache.org/en/latest/installguide/management-server/#install-the-database-server
 sudo cloudstack-setup-databases -h

#-- Create the cloud and cloud_usage databases
CREATE DATABASE `cloud`;
CREATE DATABASE `cloud_usage`;

#-- Create the cloud user
CREATE USER cloud@`localhost` identified by '<password>';
CREATE USER cloud@`%` identified by '<password>';

#-- Grant all privileges to the cloud user on the databases
GRANT ALL ON cloud.* to cloud@`localhost`;
GRANT ALL ON cloud.* to cloud@`%`;

GRANT ALL ON cloud_usage.* to cloud@`localhost`;
GRANT ALL ON cloud_usage.* to cloud@`%`;

#-- Grant process list privilege for all other databases
GRANT process ON *.* TO cloud@`localhost`;
GRANT process ON *.* TO cloud@`%`;
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
if [ $(uname -r) = "6.11.0-1004-raspi" ]
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
apt-get install -y ntpdate sudo vim htop tar iotop bridge-utils cpu-checker mysql-server

ntpdate time.nist.gov

update_mysqld() {
echo "****** update_mysqld ******"

cat <<HereDoc >> /etc/mysql/mysql.conf.d/mysqld.cnf
server_id = 1
sql-mode="STRICT_TRANS_TABLES,NO_ENGINE_SUBSTITUTION,ERROR_FOR_DIVISION_BY_ZERO,NO_ZERO_DATE,NO_ZERO_IN_DATE,NO_ENGINE_SUBSTITUTION"
innodb_rollback_on_timeout=1
innodb_lock_wait_timeout=600
max_connections=1000
log-bin=mysql-bin
binlog-format = 'ROW'

default-authentication-plugin=mysql_native_password
HereDoc
}

if [ $(grep "ERROR_FOR_DIVISION_BY_ZERO" /etc/mysql/mysql.conf.d/mysqld.cnf) ]
then
        echo "mysqld.cnf already updated"
        return
else
        echo "updating mysqld.cnf"
        update_mysqld
fi

echo "****** restart mysql ******"
systemctl restart mysql


# Setup Repo
echo "****** setup apt repo ******"
mkdir -p /etc/apt/keyrings
wget -O- http://packages.shapeblue.com/release.asc | gpg --dearmor | sudo tee /etc/apt/keyrings/cloudstack.gpg > /dev/null
echo deb [signed-by=/etc/apt/keyrings/cloudstack.gpg] http://packages.shapeblue.com/cloudstack/upstream/debian/4.19 / > /etc/apt/sources.list.d/cloudstack.list

# Install management server
echo "****** install cloudstack ******"
apt-get update
apt-get install cloudstack-management cloudstack-usage

# Stop the automatic start after install
systemctl stop cloudstack-management cloudstack-usage

echo "****** setup database ******"
cloudstack-setup-databases cloud:cloud@localhost --deploy-as=root: -i 192.168.1.10

echo "****** kvm setup ******"
apt-get install -y qemu-kvm cloudstack-agent
systemctl stop cloudstack-agent
sed -i -e 's/\#vnc_listen.*$/vnc_listen = "0.0.0.0"/g' /etc/libvirt/qemu.conf
echo 'security_driver = "none"' >> /etc/libvirt/qemu.conf
echo LIBVIRTD_ARGS=\"--listen\" >> /etc/default/libvirtd

echo 'listen_tls=0' >> /etc/libvirt/libvirtd.conf
echo 'listen_tcp=1' >> /etc/libvirt/libvirtd.conf
echo 'tcp_port = "16509"' >> /etc/libvirt/libvirtd.conf
echo 'mdns_adv = 0' >> /etc/libvirt/libvirtd.conf
echo 'auth_tcp = "none"' >> /etc/libvirt/libvirtd.conf

echo "****** configure iptables ******"
echo "doing nothing"

echo "****** mysql defaults ******"
cat /etc/mysql/debian.cnf

# exit sudo
exit


echo "****** start cloudstack ******"
cloudstack-setup-management
systemctl status cloudstack-management
tail -f /var/log/cloudstack/management/management-server.log

