NAME = webserv

# ソースとヘッダの場所
SRC_DIR = src
INC_DIR = include

# ソース一覧（今後 cpp を追加すればここに並べる）
SRC = $(SRC_DIR)/main.cpp \
      $(SRC_DIR)/Server.cpp \
      $(SRC_DIR)/log.cpp \
	  $(SRC_DIR)/RequestParser.cpp \
      $(SRC_DIR)/ServerManager.cpp \
	  $(SRC_DIR)/resp/ErrorPages.cpp \
      $(SRC_DIR)/resp/Mime.cpp \
      $(SRC_DIR)/resp/ResponseBuilder.cpp \
	  $(SRC_DIR)/ConfigParser.cpp \

OBJ = $(SRC:.cpp=.o)

CXX = g++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -I$(INC_DIR)

all: $(NAME)

$(NAME): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(NAME) $(OBJ)

clean:
	rm -f $(OBJ)

fclean: clean
	rm -f $(NAME)

re: fclean all
