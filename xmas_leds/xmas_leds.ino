#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>

#define WAND_PIN A7
#define WAND_BTN_THRESH 900

#define PIN 2
#define LED_COUNT 60
#define F_LED_COUNT 60.0

#define DEBOUNCE 40
#define WAND_TRACK_SPEED 3
#define WAND_FIND_SPEED 30
//#define DEBUG
#define WAND_FAILSAFE_POLLING 1000
//game stuff
#define GAME_LEDS 60
#define BOARD_SIZE 16
#define WHITE_SPACE 0x00ffffff
#define BLACK_SPACE 0x00402000
#define BG_COLOR 0x00804000
#define VALID_MOVE_COLOR 0x00408000
#define MOVES_BEFORE_STALE 16


//pieces range from -6 to 6 where negatives are "black", 0 is nothing, and positives are "white". add 6 to index PIECE_COLORS.
const uint32_t PIECE_COLORS[13] = {0x00330033, 0x00000033, 0x00003333, 0x00003300, 0x00333300, 0x00330000, 0xffffffff, 0x00ff0000, 0x00ffff00, 0x0000ff00, 0x0000ffff, 0x000000ff, 0x00ff00ff};
//captured: 0 to 5 white side, 6 to 9 black side
//lastMove: what,captured,from,to
int8_t board[BOARD_SIZE], captured[10], lastMove[4];
bool player, pawnFirst[2], inCheck, go;
uint16_t validMoves[6];
uint32_t wandLightThresh = 470;
uint8_t staleMoves = 0;
bool inGame;

//leds
Adafruit_NeoPixel strip = Adafruit_NeoPixel(LED_COUNT, PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  // put your setup code here, to run once:
#ifdef DEBUG
  Serial.begin(9600);
#endif
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  calibrateWand();
}

uint8_t find2pow(uint16_t v)
{
  uint16_t j = 1;
  for (uint8_t i = 0; i < 16; i++)
  {
    if (j > v)
    {
      return i;
    }
    j *= 2;
  }
}

void calibrateWand()
{
  EEPROM.get(0,wandLightThresh);
  if(!readBtn())return;
  uint32_t a=1024,b=0,val = 0,tmp,count = 0;
  for (uint16_t i = 0; i < GAME_LEDS; i++)
  {
    strip.setPixelColor(i, i%2==0?0x00000000:0x00ffffff);
  }
  strip.show();
  delay(DEBOUNCE);
  while (readBtn());
  delay(DEBOUNCE);
  while (!readBtn());
  delay(DEBOUNCE);
  while(readBtn())
  {
    tmp=analogRead(WAND_PIN);
    a=min(a,tmp);
    b=max(b,tmp);
    delay(WAND_TRACK_SPEED);
  }
  delay(DEBOUNCE);
  wandLightThresh = (a+b)/2;
  EEPROM.put(0,wandLightThresh);
#ifdef DEBUG
  Serial.println(F("min,max,thresh:"));
  Serial.print(a);
  Serial.print(F(","));
  Serial.print(b);
  Serial.print(F(","));
  Serial.println(wandLightThresh);
#endif
  //while (readBtn());
  //delay(DEBOUNCE);
}

//My indexOf() for arrays
int8_t myIndexOf(int8_t a[], int8_t v, int8_t len) {
  int8_t i;
  for (i = 0; i < len && a[i] != v; i++);
  return (a[i] == v) ? i : -1;
}

//sign function (aka. polarity)
int sign(int v) {
  return v == 0 ? 0 : v / abs(v);
}

//check button
bool readBtn()
{
  return analogRead(WAND_PIN) <= WAND_BTN_THRESH;
}

//check button and read position -1=released too soon -2=not detected
int readWand()
{
  if (!readBtn())return -1;
  delay(DEBOUNCE);
  uint8_t bits = find2pow(GAME_LEDS);
  for (uint16_t i = 0; i < GAME_LEDS; i++)
  {
    strip.setPixelColor(i, 0x00ffffff);
  }
  strip.show();
  delay(WAND_FIND_SPEED);
  if (analogRead(WAND_PIN) >= wandLightThresh)return -2;
  uint16_t ret = 0, j = 1;
  for (uint8_t i = 0; i < bits; i++)
  {
    for (uint16_t k = 0; k < GAME_LEDS; k++)
    {
      strip.setPixelColor(k, (k / j) % 2 == 0 ? 0x00000000 : 0x00ffffff);
    }
    strip.show();
    delay(WAND_FIND_SPEED);
    if (!readBtn())return -1;
    if (analogRead(WAND_PIN) < wandLightThresh)ret |= j;
    j *= 2;
  }
  return ret;
}

//wait for button release
void waitBtnReleased()
{
  while (readBtn());
  delay(DEBOUNCE);
}

//is a valid board space
bool onBoard(int8_t v) {
  return v >= 0 && v < BOARD_SIZE;
}

//is the move valid
bool isMoveValid(int8_t piece, int8_t to)
{
  int8_t what;
  what = abs(piece) - 1;
  if (what < 0 || !onBoard(to))return false;
  return bitRead(validMoves[what], to);
}

//undo last move. true on error
bool undoMove()
{
  if (lastMove[0] == 0)return true;
  board[lastMove[2]] = lastMove[0];
  board[lastMove[3]] = lastMove[1];
  for (uint8_t i = 0; i < 3; i++)lastMove[i] = 0;
  return false;
}

//move a piece. true on error
bool doMove(int8_t from, int8_t to)
{
  int8_t side = player ? -1 : 1;
  int8_t piece = 0;
  if (!onBoard(from) || !onBoard(to))return true;
  piece = board[from];
  if (sign(piece) != side)return true;
  if (!isMoveValid(piece, to))return true;
  lastMove[0] = piece;
  lastMove[1] = board[to];
  lastMove[2] = from;
  lastMove[3] = to;
  board[to] = piece;
  board[from] = 0;
  return false;
}

//test if in check
bool isInCheck()
{
  int8_t side = player ? -1 : 1;
  int8_t pos = -1, piece = 0, space = 0, king = 6 * side;
  //pawn
  piece = -side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  if (pos > -1)
  {
    if (board[pos - side] == king)
    {
      //first not self
      return true;
    }
    else if (board[pos - side] == 0 && board[pos - side * 2] == king && pawnFirst[!player])
    {
      return true;
    }
  }
  //knight
  piece = -2 * side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  if (pos > -1)
  {
    space = pos - side * 3;
    if (board[space] == king)return true;
    space = pos - side * 2;
    if (board[space] == king)return true;
    space = pos + side * 3;
    if (board[space] == king)return true;
    space = pos + side * 2;
    if (board[space] == king)return true;
  }
  //bishop
  piece = -3 * side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  if (pos > -1)
  {
    //positive dir
    for (space = pos + 2; space < BOARD_SIZE && board[space] == 0; space += 2);
    if (board[space] == king)return true;
    //negative dir
    for (space = pos - 2; space > -1 && board[space] == 0; space -= 2);
    if (board[space] == king)return true;
  }
  //rook
  piece = -4 * side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  if (pos > -1)
  {
    //positive dir
    for (space = pos + 1; space < BOARD_SIZE && board[space] == 0; space++);
    if (board[space] == king)return true;
    //negative dir
    for (space = pos - 1; space > -1 && board[space] == 0; space--);
    if (board[space] == king)return true;
  }
  //queen
  piece = -5 * side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  if (pos > -1)
  {
    //bishop like
    //positive dir
    for (space = pos + 2; space < BOARD_SIZE && board[space] == 0; space += 2);
    if (board[space] == king)return true;
    //negative dir
    for (space = pos - 2; space > -1 && board[space] == 0; space -= 2);
    if (board[space] == king)return true;
    //rook like
    //positive dir
    for (space = pos + 1; space < BOARD_SIZE && board[space] == 0; space++);
    if (board[space] == king)return true;
    //negative dir
    for (space = pos - 1; space > -1 && board[space] == 0; space--);
    if (board[space] == king)return true;
  }
  return false;
}
//check space. -1=off board 0=self 1=opposite 2=free
int8_t checkSpace(int8_t space)
{
  int8_t side = player ? -1 : 1;
  if (onBoard(space))
  {
    if (sign(board[space]) == side)return 0;
    if (sign(board[space]) == -side)return 1;
    return 2;
  }
  return -1;
}

//gets valid moves for each piece
void getValidMoves()
{
  int8_t side = player ? -1 : 1;
  int8_t pos = -1, piece = 0, space = 0;
  //pawn
  piece = side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  validMoves[0] = 0x0000;
  if (pos > -1)
  {
    if (checkSpace(pos + side) > 0)
    {
      //first not self
      bitSet(validMoves[0], pos + side);
      //first move rule
      if (checkSpace(pos + side) == 2 && checkSpace(pos + side * 2) > 1 && pawnFirst[player])bitSet(validMoves[0], pos + side * 2);
    }
  }
  //knight
  piece = 2 * side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  validMoves[1] = 0x0000;
  if (pos > -1)
  {
    space = pos - side * 3;
    if (checkSpace(space) > 0)bitSet(validMoves[1], space);
    space = pos - side * 2;
    if (checkSpace(space) > 0)bitSet(validMoves[1], space);
    space = pos + side * 3;
    if (checkSpace(space) > 0)bitSet(validMoves[1], space);
    space = pos + side * 2;
    if (checkSpace(space) > 0)bitSet(validMoves[1], space);
  }
  //bishop
  piece = 3 * side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  validMoves[2] = 0x0000;
  if (pos > -1)
  {
    //positive dir
    for (space = pos + 2; space < BOARD_SIZE && checkSpace(space) == 2; space += 2)
    {
      bitSet(validMoves[2], space);
    }
    if (checkSpace(space) > 0)bitSet(validMoves[2], space);
    //negative dir
    for (space = pos - 2; space > -1 && checkSpace(space) == 2; space -= 2)
    {
      bitSet(validMoves[2], space);
    }
    if (checkSpace(space) > 0)bitSet(validMoves[2], space);
  }
  //rook
  piece = 4 * side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  validMoves[3] = 0x0000;
  if (pos > -1)
  {
    //positive dir
    for (space = pos + 1; space < BOARD_SIZE && checkSpace(space) == 2; space++)
    {
      bitSet(validMoves[3], space);
    }
    if (checkSpace(space) > 0)bitSet(validMoves[3], space);
    //negative dir
    for (space = pos - 1; space > -1 && checkSpace(space) == 2; space--)
    {
      bitSet(validMoves[3], space);
    }
    if (checkSpace(space) > 0)bitSet(validMoves[3], space);
  }
  //queen
  piece = 5 * side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  validMoves[4] = 0x0000;
  if (pos > -1)
  {
    //bishop like
    //positive dir
    for (space = pos + 2; space < BOARD_SIZE && checkSpace(space) == 2; space += 2)
    {
      bitSet(validMoves[4], space);
    }
    if (checkSpace(space) > 0)bitSet(validMoves[4], space);
    //negative dir
    for (space = pos - 2; space > -1 && checkSpace(space) == 2; space -= 2)
    {
      bitSet(validMoves[4], space);
    }
    if (checkSpace(space) > 0)bitSet(validMoves[4], space);
    //rook like
    //positive dir
    for (space = pos + 1; space < BOARD_SIZE && checkSpace(space) == 2; space++)
    {
      bitSet(validMoves[4], space);
    }
    if (checkSpace(space) > 0)bitSet(validMoves[4], space);
    //negative dir
    for (space = pos - 1; space > -1 && checkSpace(space) == 2; space--)
    {
      bitSet(validMoves[4], space);
    }
    if (checkSpace(space) > 0)bitSet(validMoves[4], space);
  }
  //king
  piece = 6 * side;
  pos = myIndexOf(board, piece, BOARD_SIZE);
  validMoves[5] = 0x0000;
  if (pos > -1)
  {
    if (checkSpace(pos + 1) > 0)bitSet(validMoves[5], pos + 1);
    if (checkSpace(pos - 1) > 0)bitSet(validMoves[5], pos - 1);
  }
#ifdef DEBUG
  Serial.println(F("valid moves:"));
#endif
  //remove self check moves
  for (uint8_t i = 0; i < 6; i++)
  {
    pos = myIndexOf(board, (i + 1) * side, BOARD_SIZE);
    if (pos < 0)
    {
#ifdef DEBUG
      Serial.println(0, BIN);
#endif
      continue;
    }
    for (uint8_t j = 0; j < BOARD_SIZE; j++)
    {
      if (bitRead(validMoves[i], j) == 0)continue;
      doMove(pos, j);
      if (isInCheck())bitClear(validMoves[i], j);
      undoMove();
    }
#ifdef DEBUG
    Serial.println(validMoves[i], BIN);
#endif
  }
  //end game stuff
  go = true;
  for (uint8_t i = 0; i < 6; i++)
  {
    if (validMoves[i] > 0x0000)
    {
      go = false;
      break;
    }
  }
  if (go)
  {
    if (inCheck)
    {
      //checkmate
      player = !player;
      winnerAnimation();
    }
    else
    {
      //stalemate
      stalemateAnimation();
    }
  }
}
//show winner
void winnerAnimation()
{
  paintBoardBack();
  paintBoard();
  strip.show();
  delay(3000);
  if (player)
  {
    binNoise2(BLACK_SPACE, 0x00000000, 66, 150);
  }
  else
  {
    binNoise2(WHITE_SPACE, 0x00000000, 66, 150);
  }
}

//stalemate animation
void stalemateAnimation()
{
  paintBoardBack();
  paintBoard();
  strip.show();
  delay(3000);
  binNoise2(BLACK_SPACE, WHITE_SPACE, 66, 150);
}

//paints valid moves
void paintValidMoves(int8_t what)
{
  if (what < 0)return;
  for (uint8_t i = 0; i < BOARD_SIZE; i++)
  {
    if (bitRead(validMoves[what], player ? BOARD_SIZE - 1 - i : i))
    {
      strip.setPixelColor(6 + i * 3, VALID_MOVE_COLOR);
      strip.setPixelColor(6 + i * 3 + 1, VALID_MOVE_COLOR);
      strip.setPixelColor(6 + i * 3 + 2, VALID_MOVE_COLOR);
    }
  }
}

//a players turn
void playerTurn()
{
#ifdef DEBUG
  Serial.print(F("Player "));
  Serial.println(player);
#endif
  waitBtnReleased();
  inCheck = isInCheck();
#ifdef DEBUG
  if (inCheck)Serial.println(F("In Check"));
  Serial.print(F("Board: "));
  for (int i = 0; i < BOARD_SIZE; i++)
  {
    Serial.print(board[i]);
    Serial.print(F(","));
  }
  Serial.println(F(""));
  Serial.println(F("Catured: "));
  for (int i = 0; i < 10; i++)
  {
    Serial.print(captured[i]);
    Serial.print(F(","));
  }
  Serial.println(F(""));
#endif
  staleMoves++;
  if (lastMove[1] != 0 || inCheck)staleMoves = 0;
  if (staleMoves >= MOVES_BEFORE_STALE)
  {
    go = true;
    stalemateAnimation();
    return;
  }
  for (uint8_t i = 0; i < 3; i++)lastMove[i] = 0;
  getValidMoves();
  if (go)return;
  paintBoardBack();
  paintBoard();
  strip.show();
  bool moved = false;
  int8_t from = -1, to = 0, pos = -1;
  int8_t side = player ? -1 : 1;
  while (!moved)
  {
    pos = readWand();
    if (pos < 0)
    {
        if(pos > -2)
        {
        paintBoardBack();
        if (onBoard(from))
        {
          if (sign(board[from]) == side)paintValidMoves(abs(board[from]) - 1);
        }
        paintBoard();
        strip.show();
      }
      //delay(1000);
      continue;
    }
    from = ((player ? 59 - pos : pos) - 6) / 3;
    bool lastDir = true;
    uint32_t c;
    while (readBtn())
    {
      while (pos < 0 || pos >= GAME_LEDS)
      {
        pos = readWand();
      }
      to = ((player ? 59 - pos : pos) - 6) / 3;
      paintBoardBack();
      if (sign(board[from]) == side)paintValidMoves(abs(board[from]) - 1);
      paintBoard();
      strip.setPixelColor(pos, 0x00000000);
      c=isMoveValid(board[from], to)?0x0000ff00:0x00ffffff;
      strip.setPixelColor(pos + 1, c);
      strip.setPixelColor(pos - 1, c);
      strip.show();
      delay(WAND_TRACK_SPEED);
      while (analogRead(WAND_PIN) >= wandLightThresh && readBtn())
      {
        if (millis() % WAND_FAILSAFE_POLLING == 0)
        {
          strip.setPixelColor(pos, 0x00ffffff);
          strip.show();
          delay(WAND_TRACK_SPEED);
          if (analogRead(WAND_PIN) >= wandLightThresh)
          {
            pos = readWand();
            while (pos < 0)
            {
              pos = readWand();
            }
            to = ((player ? 59 - pos : pos) - 6) / 3;
            paintBoardBack();
            if (sign(board[from]) == side)paintValidMoves(abs(board[from]) - 1);
            paintBoard();
            c=isMoveValid(board[from], to)?0x0000ff00:0x00ffffff;
            strip.setPixelColor(pos + 1, c);
            strip.setPixelColor(pos - 1, c);
          }
          strip.setPixelColor(pos, 0x00000000);
          strip.show();
          delay(WAND_TRACK_SPEED);
        }
      }
      if (lastDir)
      {
        strip.setPixelColor(pos + 1, 0x00000000);
        strip.show();
        delay(WAND_TRACK_SPEED);
        if (analogRead(WAND_PIN) < wandLightThresh)
        {
          pos--;
          lastDir = false;
        }
        else
        {
          pos++;
          lastDir = true;
        }
      }
      else
      {
        strip.setPixelColor(pos - 1, 0x00000000);
        strip.show();
        delay(WAND_TRACK_SPEED);
        if (analogRead(WAND_PIN) < wandLightThresh)
        {
          pos++;
          lastDir = true;
        }
        else
        {
          pos--;
          lastDir = false;
        }
      }
    }
    //forfeit
    if (onBoard(from))
    {
      if (board[from] == 6 * side && !onBoard(to))
      {
        go = true;
        player = !player;
        winnerAnimation();
        return;
      }
    }
    moved = !doMove(from, to);
    waitBtnReleased();
    paintBoardBack();
    if (onBoard(from))
    {
      if (sign(board[from]) == side)paintValidMoves(abs(board[from]) - 1);
    }
    paintBoard();
    strip.show();
  }
  //pawn
  if (abs(lastMove[0]) == 1)
  {
    pawnFirst[player ? 1 : 0] = false;
  }
  //capture
  if (lastMove[1] != 0)
  {
    for (uint8_t i = 0; i < 5; i++)
    {
      if (captured[i + (player ? 5 : 0)] == 0)
      {
        captured[i + (player ? 5 : 0)] = lastMove[1];
        break;
      }
    }
  }
  inCheck = false;
  paintBoardBack();
  paintBoard();
  strip.show();
  delay(3000);
  player = !player;
}
//paints board backing but does not show
void paintBoardBack()
{
  for (int i = 0; i < GAME_LEDS; i++)
  {
    if (i >= 6 && i < 54)
    {
      strip.setPixelColor(i, ((i - 6) / 3) % 2 == player ? WHITE_SPACE : BLACK_SPACE);
    }
    else
    {
      strip.setPixelColor(i, BG_COLOR);
    }
  }
}

//paints pieces of the board but does not show
void paintBoard()
{
  if (player)
  {
    for (int i = 0; i < BOARD_SIZE; i++)if (board[i] != 0)strip.setPixelColor(GAME_LEDS - 1 - (7 + i * 3), PIECE_COLORS[board[i] + 6]);
    for (int i = 0; i < 5; i++)
    {
      if (captured[i + 5] != 0)strip.setPixelColor(i, PIECE_COLORS[captured[i + 5] + 6]);
      if (captured[i] != 0)strip.setPixelColor(GAME_LEDS - 1 - i, PIECE_COLORS[captured[i] + 6]);
    }
  }
  else
  {
    for (int i = 0; i < BOARD_SIZE; i++)if (board[i] != 0)strip.setPixelColor(7 + i * 3, PIECE_COLORS[board[i] + 6]);
    for (int i = 0; i < 5; i++)
    {
      if (captured[i] != 0)strip.setPixelColor(i, PIECE_COLORS[captured[i] + 6]);
      if (captured[i + 5] != 0)strip.setPixelColor(GAME_LEDS - 1 - i, PIECE_COLORS[captured[i + 5] + 6]);
    }
  }
  if (inCheck)
  {
    for (int i = 0; i < 6; i++)strip.setPixelColor(i, WHITE_SPACE);
  }
}

//1D chess game
void game()
{
  if (inGame)
  {
    waitBtnReleased();
    player = false;
    pawnFirst[0] = true;
    pawnFirst[1] = true;
    staleMoves = 0;
    for (int i = 0; i < 10; i++)captured[i] = 0;
    for (int i = 6; i < 10; i++)board[i] = 0;
    for (int i = 0; i < 6; i++)
    {
      board[i] = 6 - i;
      board[15 - i] = i - 6;
    }
    //game loop
    go = false;
    while (!go)
    {
      playerTurn();
    }
  }
  waitBtnReleased();
  inGame = false;
}

// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  for (uint16_t i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
    if (readBtn() && !inGame)
    {
      inGame = true;
      return;
    }
  }
}

//Theatre-style crawling lights.
void theaterChase(uint32_t c1, uint32_t c2, uint32_t c3, uint8_t wait, uint16_t cycles, uint8_t width) {
  uint16_t pat_len = strip.numPixels() / width / 3 + 1;
  pat_len *= width * 3;
  for (int j = 0; j < cycles; j++)
  {
    for (int q = 0; q < 3 * width; q++)
    {
      for (uint16_t i = 0; i < strip.numPixels(); i = i + 3 * width)
      {
        for (int w = 0; w < width; w++)
        {
          strip.setPixelColor((i + q + w) % pat_len, c1); //turn every third pixel on
          strip.setPixelColor((i + q + w + width) % pat_len, c2); //turn every third pixel on
          strip.setPixelColor((i + q + w + width * 2) % pat_len, c3); //turn every third pixel on
        }
      }
      strip.show();
      delay(wait);
      if (readBtn() && !inGame)
      {
        inGame = true;
        return;
      }
    }
  }
}

void theaterChase2TheaterChase(uint32_t c1a, uint32_t c2a, uint32_t c3a, uint32_t c1b, uint32_t c2b, uint32_t c3b, uint8_t wait, uint8_t width) {
  uint16_t pat_len = strip.numPixels() / width / 3 + 1;
  pat_len *= width * 3;
  for (int j = 0; j < pat_len; j++)
  {
    int q = j % (3 * width);
    //3*width
    for (int i = 0; i < strip.numPixels(); i = i + 3 * width)
    {
      for (int w = 0; w < width; w++)
      {
        strip.setPixelColor((i + q + w) % pat_len, ((i + q + w) % pat_len < j) ? c1b : c1a); //turn every third pixel on
        strip.setPixelColor((i + q + w + width) % pat_len, ((i + q + w + width) % pat_len < j) ? c2b : c2a); //turn every third pixel on
        strip.setPixelColor((i + q + w + width * 2) % pat_len, ((i + q + w + width * 2) % pat_len < j) ? c3b : c3a); //turn every third pixel on
      }
    }
    strip.show();
    delay(wait);
    if (readBtn() && !inGame)
    {
      inGame = true;
      return;
    }
  }
}

void theaterChase2Solid(uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c, uint8_t wait, uint8_t width)
{
  uint16_t pat_len;
  pat_len = strip.numPixels() / width / 3 + 1;
  pat_len *= width * 3;
  for (int j = 0; j < strip.numPixels() + 1; j++)
  {
    int q = j % (3 * width);
    //3*width
    for (int i = 0; i < strip.numPixels(); i = i + 3 * width)
    {
      for (int w = 0; w < width; w++)
      {
        strip.setPixelColor((i + q + w) % pat_len, ((i + q + w) % pat_len < j) ? c : c1); //turn every third pixel on
        strip.setPixelColor((i + q + w + width) % pat_len, ((i + q + w + width) % pat_len < j) ? c : c2); //turn every third pixel on
        strip.setPixelColor((i + q + w + width * 2) % pat_len, ((i + q + w + width * 2) % pat_len < j) ? c : c3); //turn every third pixel on
      }
    }
    strip.show();
    delay(wait);
    if (readBtn() && !inGame)
    {
      inGame = true;
      return;
    }
  }
}

void binNoise2(uint32_t c1, uint32_t c2, uint8_t wait, int frames)
{
  for (int j = 0; j < frames; j++)
  {
    for (uint16_t i = 0; i < strip.numPixels(); i++)
    {
      strip.setPixelColor(i, random(2) ? c2 : c1);
    }
    strip.show();
    delay(wait);
    if (readBtn() && !inGame)
    {
      inGame = true;
      return;
    }
  }
}
void binNoise3(uint32_t c1, uint32_t c2, uint32_t c3, uint8_t wait, int frames)
{
  for (int j = 0; j < frames; j++)
  {
    for (uint16_t i = 0; i < strip.numPixels(); i++)
    {
      switch (random(3))
      {
        case 0:
          strip.setPixelColor(i, c1);
          break;
        case 1:
          strip.setPixelColor(i, c2);
          break;
        case 2:
          strip.setPixelColor(i, c3);
          break;
      }
    }
    strip.show();
    delay(wait);
    if (readBtn() && !inGame)
    {
      inGame = true;
      return;
    }
  }
}
void noiseMono(uint32_t c, uint8_t wait, uint16_t frames)
{
  for (int j = 0; j < frames; j++)
  {
    for (uint16_t i = 0; i < strip.numPixels(); i++)
    {
      uint8_t l = random(256);
      uint32_t r = (c >> 16 & 0x000000ff) * l / 255, g = (c >> 8 & 0x000000ff) * l / 255, b = (c & 0x000000ff) * l / 255;
      uint32_t c1 = r << 16 | g << 8 | b;
      strip.setPixelColor(i, c1);
    }
    strip.show();
    delay(wait);
    if (readBtn() && !inGame)
    {
      inGame = true;
      return;
    }
  }
}
void noise(uint8_t wait, uint16_t frames)
{
  for (int j = 0; j < frames; j++)
  {
    for (uint16_t i = 0; i < strip.numPixels(); i++)
    {
      uint32_t r = random(256), g = random(256), b = random(256);
      uint32_t c1 = r << 16 | g << 8 | b;
      strip.setPixelColor(i, c1);
    }
    strip.show();
    delay(wait);
    if (readBtn() && !inGame)
    {
      inGame = true;
      return;
    }
  }
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait, unsigned int skip, int cycles) {
  uint16_t i, j;
  for (int k = 0; k < cycles; k++)
  {
    for (j = 0; j < 256 * 6; j += skip)
    { // 1 cycles of all colors on wheel
      for (i = 0; i < strip.numPixels(); i++)
      {
        strip.setPixelColor(i, Wheel(i / F_LED_COUNT + 1 - j / 256.0 / 6.0));
      }
      strip.show();
      delay(wait);
      if (readBtn() && !inGame)
      {
        inGame = true;
        return;
      }
    }
  }
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(float h) {
  byte x;
  float h6x;
  h6x = fmod(h * 6.0, 6.0);
  x = (1.0 - fabs(fmod(h6x, 2.0) - 1.0)) * 255;
  if (h6x < 1)
  {
    return strip.Color(255, x, 0);
  }
  else if (h6x < 2)
  {
    return strip.Color(x, 255, 0);
  }
  else if (h6x < 3)
  {
    return strip.Color(0, 255, x);
  }
  else if (h6x < 4)
  {
    return strip.Color(0, x, 255);
  }
  else if (h6x < 5)
  {
    return strip.Color(x, 0, 255);
  }
  else
  {
    return strip.Color(255, 0, x);
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  //readWand(0x00ff0000);
  //waitBtnReleased();

  binNoise2(0x00ff0000, 0x0000ff00, 66, 150);
  game();
  binNoise3(0x00ff0000, 0x00ffffff, 0x000000ff, 66, 150);
  game();
  noiseMono(0x00ff0000, 66, 45);
  game();
  noiseMono(0x00ffffff, 66, 45);
  game();
  noiseMono(0x000000ff, 66, 45);
  game();
  noise(66, 90);
  game();
  rainbowCycle(1, 16, 10);
  game();
  theaterChase(0x00ff0000, 0x00000000, 0x00000000, 100, 10, 3);
  game();
  theaterChase2TheaterChase(0x00ff0000, 0x00000000, 0x00000000, 0x00ff0000, 0x00ffffff, 0x00ffffff, 100, 3);
  game();
  theaterChase(0x00ff0000, 0x00ffffff, 0x00ffffff, 100, 10, 3);
  game();
  theaterChase2TheaterChase(0x00ff0000, 0x00ffffff, 0x00ffffff, 0x000000ff, 0x00ffffff, 0x00ffffff, 100, 3);
  game();
  theaterChase(0x000000ff, 0x00ffffff, 0x00ffffff, 100, 10, 3);
  game();
  theaterChase2Solid(0x000000ff, 0x00ffffff, 0x00ffffff, 0x00ff0000, 100, 3);
  game();
  colorWipe(0x00ffffff, 100);
  game();
  colorWipe(0x000000ff, 100);
  game();

}
