echo swrite 1m
sudo filebench -f ./swrite_1m.f > ./swrite_1m_0.txt
sudo filebench -f ./swrite_1m.f > ./swrite_1m_1.txt
sudo filebench -f ./swrite_1m.f > ./swrite_1m_2.txt


echo swrite 4m
sudo filebench -f ./swrite_4m.f > ./swrite_4m_0.txt
sudo filebench -f ./swrite_4m.f > ./swrite_4m_1.txt
sudo filebench -f ./swrite_4m.f > ./swrite_4m_2.txt


echo swrite 8m
sudo filebench -f ./swrite_8m.f > ./swrite_8m_0.txt
sudo filebench -f ./swrite_8m.f > ./swrite_8m_1.txt
sudo filebench -f ./swrite_8m.f > ./swrite_8m_2.txt