"""Verify each actual maze layout has a ball-radius-safe route to the goal."""
import ast
from collections import deque
from pathlib import Path
s=Path('src/apps/games.c').read_text()
s=s.split('maze_walls[5][4][4] =',1)[1].split(';',1)[0]
levels=ast.literal_eval(s.replace('{','[').replace('}',']'))
for level,walls in enumerate(levels,1):
 def valid(x,y):
  if not (6<=x<=294 and 6<=y<=149):return False
  return not any(x+6>a and x-6<a+w and y+6>b*.8 and y-6<(b+h)*.8 for a,b,w,h in walls)
 q=deque([(18,18)]);seen=set(q);goal=False
 while q:
  x,y=q.popleft()
  if x>273 and y>125:goal=True;break
  for p in [(x+1,y),(x-1,y),(x,y+1),(x,y-1)]:
   if p not in seen and valid(*p):seen.add(p);q.append(p)
 assert goal,f'Level {level} is unsolvable'
 print(f'Level {level}: route to goal PASS')
