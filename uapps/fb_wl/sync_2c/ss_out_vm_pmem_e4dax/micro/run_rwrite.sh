echo rwrite 1k
sudo filebench -f ./rwrite_1k.f > ./rwrite_1k_0.txt
sudo filebench -f ./rwrite_1k.f > ./rwrite_1k_1.txt
sudo filebench -f ./rwrite_1k.f > ./rwrite_1k_2.txt


echo rwrite 4k
sudo filebench -f ./rwrite_4k.f > ./rwrite_4k_0.txt
sudo filebench -f ./rwrite_4k.f > ./rwrite_4k_1.txt
sudo filebench -f ./rwrite_4k.f > ./rwrite_4k_2.txt


echo rwrite 16k
sudo filebench -f ./rwrite_16k.f > ./rwrite_16k_0.txt
sudo filebench -f ./rwrite_16k.f > ./rwrite_16k_1.txt
sudo filebench -f ./rwrite_16k.f > ./rwrite_16k_2.txt