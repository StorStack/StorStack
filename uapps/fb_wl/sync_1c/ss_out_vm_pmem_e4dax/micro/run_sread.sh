echo sread 1m
sudo filebench -f ./sread_1m.f > ./sread_1m_0.txt
sudo filebench -f ./sread_1m.f > ./sread_1m_1.txt
sudo filebench -f ./sread_1m.f > ./sread_1m_2.txt


echo sread 4m
sudo filebench -f ./sread_4m.f > ./sread_4m_0.txt
sudo filebench -f ./sread_4m.f > ./sread_4m_1.txt
sudo filebench -f ./sread_4m.f > ./sread_4m_2.txt


echo sread 8m
sudo filebench -f ./sread_8m.f > ./sread_8m_0.txt
sudo filebench -f ./sread_8m.f > ./sread_8m_1.txt
sudo filebench -f ./sread_8m.f > ./sread_8m_2.txt