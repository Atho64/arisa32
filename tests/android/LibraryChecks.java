package org.dc4engine;
import java.io.*;
import java.nio.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
public class LibraryChecks {
 static int assertions;
 static final String GAME="a".repeat(64), CONTENT="b".repeat(64);
 static void check(boolean ok,String message){assertions++;if(!ok)throw new AssertionError(message);}
 static byte[] save(int seed) {
  byte[] payload=new byte[64]; Arrays.fill(payload,(byte)seed);
  long hash=0xcbf29ce484222325L; for(byte b:payload){hash^=b&255;hash*=1099511628211L;}
  return ByteBuffer.allocate(24+payload.length).order(ByteOrder.LITTLE_ENDIAN).put(new byte[]{'D','C','4','S','A','V','E',0}).putInt(4).putInt(payload.length).putLong(hash).put(payload).array();
 }
 static byte[] archive(String game,String content,Map<String,byte[]> entries,boolean manifest)throws Exception{
  ByteArrayOutputStream bytes=new ByteArrayOutputStream();
  try(ZipOutputStream z=new ZipOutputStream(bytes)){
   if(manifest){z.putNextEntry(new ZipEntry("manifest.properties"));Properties p=new Properties();p.setProperty("format","dc4engine-saves-1");p.setProperty("game",game);p.setProperty("content",content);p.store(z,"");z.closeEntry();}
   for(Map.Entry<String,byte[]> e:entries.entrySet()){z.putNextEntry(new ZipEntry(e.getKey()));z.write(e.getValue());z.closeEntry();}
  }return bytes.toByteArray();
 }
 static void rejected(File saves,byte[] zip,String reason)throws Exception{
  byte[] before=Files.readAllBytes(new File(saves,"slot1.dc4sav").toPath());
  boolean failed=false;try{SaveArchive.read(new ByteArrayInputStream(zip),saves,GAME,CONTENT);}catch(IOException e){failed=true;}
  check(failed,reason+" rejected");check(Arrays.equals(before,Files.readAllBytes(new File(saves,"slot1.dc4sav").toPath())),reason+" preserves original");
 }
 public static void main(String[] args)throws Exception{
  File root=Files.createTempDirectory("dc4-java-check-").toFile();
  try{
   File saves=new File(root,"saves/jp");saves.mkdirs();Files.write(new File(saves,"slot1.dc4sav").toPath(),save(1));
   byte[] valid=archive(GAME,CONTENT,Map.of("slot2.dc4sav",save(2)),true);
   rejected(saves,archive("other",CONTENT,Map.of("slot2.dc4sav",save(2)),true),"wrong game");
   rejected(saves,archive(GAME,"other",Map.of("slot2.dc4sav",save(2)),true),"wrong content");
   rejected(saves,archive(GAME,CONTENT,Map.of("slot2.dc4sav",save(2)),false),"missing manifest");
   rejected(saves,archive(GAME,CONTENT,Map.of(),true),"empty archive");
   rejected(saves,archive(GAME,CONTENT,Map.of("../escape.dc4sav",save(2)),true),"zip slip");
   rejected(saves,archive(GAME,CONTENT,Map.of("C:\\escape.dc4sav",save(2)),true),"absolute path");
   rejected(saves,archive(GAME,CONTENT,Map.of("slot2.dc4sav",new byte[2]),true),"truncated save");
   byte[] corrupted=save(2);corrupted[24]^=1;
   rejected(saves,archive(GAME,CONTENT,Map.of("slot2.dc4sav",corrupted),true),"save checksum");
   byte[] version=save(2);version[8]=99;
   rejected(saves,archive(GAME,CONTENT,Map.of("slot2.dc4sav",version),true),"save version");
   byte[] length=save(2);length[12]=22;
   rejected(saves,archive(GAME,CONTENT,Map.of("slot2.dc4sav",length),true),"save length");
   rejected(saves,archive(GAME,CONTENT,Map.of("slot2.dc4sav",save(2),"slot3.dc4sav.txt","orphan".getBytes()),true),"orphan preview");
   rejected(saves,archive(GAME,CONTENT,Map.of("slot2.dc4sav",save(2),"slot2.dc4sav.png",new byte[100001]),true),"large thumbnail");
   rejected(saves,archive(GAME,CONTENT,Map.of("slot101.dc4sav",save(2)),true),"out of range slot");
   byte[] duplicate=archive(GAME,CONTENT,new LinkedHashMap<>(Map.of("slot2.dc4sav",save(2),"slot3.dc4sav",save(3))),true);
   byte[] from="slot3.dc4sav".getBytes(),to="slot2.dc4sav".getBytes();
   for(int i=0;i<=duplicate.length-from.length;i++){boolean match=true;for(int j=0;j<from.length;j++)if(duplicate[i+j]!=from[j])match=false;if(match)System.arraycopy(to,0,duplicate,i,to.length);}
   rejected(saves,duplicate,"duplicate save");
   byte[] oldProfile="DC4ENGINE_PROFILE 1\nv 93 1 100\nc \"old/cg\"\n".replace("\\n","\n").replace("\\\"","\"").getBytes();
   byte[] newProfile="DC4ENGINE_PROFILE 1\nv 93 1 50\nc \"new/cg\"\nm \"music1\"\ns \"scenario#scene1\"\np 3\n".replace("\\n","\n").replace("\\\"","\"").getBytes();
   Files.write(new File(saves,"system-profile.dat").toPath(),oldProfile);Files.write(new File(saves,"read-lines.txt").toPath(),"scene:10\n".getBytes());Files.write(new File(saves,"autosave.dc4sav").toPath(),save(9));
   rejected(saves,archive(GAME,CONTENT,Map.of("system-profile.dat","WRONG PROFILE".getBytes()),true),"profile header");
   rejected(saves,archive(GAME,CONTENT,Map.of("system-profile.dat","DC4ENGINE_PROFILE 1\nv 1 0 1\n".getBytes()),true),"profile bank");
   rejected(saves,archive(GAME,CONTENT,Map.of("system-profile.dat","DC4ENGINE_PROFILE 1\nc \"../escape\"\n".getBytes()),true),"profile path");
   rejected(saves,archive(GAME,CONTENT,Map.of("system-profile.dat","DC4ENGINE_PROFILE 1\np 1001\n".getBytes()),true),"profile playlist bound");
   rejected(saves,archive(GAME,CONTENT,Map.of("read-lines.txt",new byte[301]),true),"read metadata null");
   rejected(saves,archive(GAME,CONTENT,Map.of("read-lines.txt","a".repeat(301).getBytes()),true),"read metadata line bound");
   boolean rejectedByCore=false;int[] validated={0};
   byte[] beforeNative=Files.readAllBytes(new File(saves,"slot1.dc4sav").toPath());
   try{SaveArchive.read(new ByteArrayInputStream(valid),saves,GAME,CONTENT,file->{validated[0]++;throw new IOException("native VM rejected snapshot");});}catch(IOException error){rejectedByCore=true;}
   check(rejectedByCore && validated[0]==1,"native validator called and aborts");
   check(Arrays.equals(beforeNative,Files.readAllBytes(new File(saves,"slot1.dc4sav").toPath())),"native rejection preserves existing saves");
   check(Arrays.equals(oldProfile,Files.readAllBytes(new File(saves,"system-profile.dat").toPath())),"native rejection preserves existing profile");
   SaveArchive.read(new ByteArrayInputStream(valid),saves,GAME,CONTENT);
   check(Arrays.equals(oldProfile,Files.readAllBytes(new File(saves,"system-profile.dat").toPath())),"old archive preserves profile");
   check(new File(saves,"read-lines.txt").isFile(),"old archive preserves read history");check(new File(saves,"autosave.dc4sav").isFile(),"old archive preserves autosave");
   SaveArchive.read(new ByteArrayInputStream(archive(GAME,CONTENT,Map.of("slot2.dc4sav",save(2),"system-profile.dat",newProfile,"read-lines.txt","scene:20\n".getBytes()),true)),saves,GAME,CONTENT);
   check(Arrays.equals(newProfile,Files.readAllBytes(new File(saves,"system-profile.dat").toPath())),"new archive replaces valid profile");
   check(new File(saves,"slot2.dc4sav").isFile(),"valid import installed");check(!new File(saves,"slot1.dc4sav").exists(),"complete set replaced");
   ByteArrayOutputStream exported=new ByteArrayOutputStream();SaveArchive.write(exported,saves,GAME,CONTENT);
   SaveArchive.read(new ByteArrayInputStream(exported.toByteArray()),saves,GAME,CONTENT);
   check(Arrays.equals(save(2),Files.readAllBytes(new File(saves,"slot2.dc4sav").toPath())),"round trip bytes");
   File backup=new File(saves.getParentFile(),"jp.import-backup");check(saves.renameTo(backup),"simulate interrupted swap");SaveArchive.recover(saves);check(new File(saves,"slot2.dc4sav").isFile(),"interrupted import recovered");
   File dataA=new File(root,"A/MES"), dataB=new File(root,"B/MES");dataA.mkdirs();dataB.mkdirs();
   Files.write(new File(dataA,"start.mes").toPath(),new byte[]{1,2,3});Files.write(new File(dataB,"start.mes").toPath(),new byte[]{1,2,3});
   String hash=GameLibrary.fingerprint(dataA.getParentFile());check(hash.equals(GameLibrary.fingerprint(dataB.getParentFile())),"identity stable across moves");
   Files.write(new File(dataB,"start.mes").toPath(),new byte[]{1,2,4});check(!hash.equals(GameLibrary.fingerprint(dataB.getParentFile())),"identity checks scripts");
   File empty=new File(root,"empty/MES");empty.mkdirs();boolean noScripts=false;try{GameLibrary.fingerprint(empty.getParentFile());}catch(IOException e){noScripts=true;}check(noScripts,"empty language pack rejected");
   GameLibrary library=new GameLibrary(root);GameLibrary.Entry e=new GameLibrary.Entry();e.id=hash;e.title="D.C.4 日本語";e.path=dataA.getParent();e.english=dataB.getParent();e.language="en";e.enEncoding="UTF-8";e.enFingerprint=CONTENT;library.entries.add(e);library.save();
   GameLibrary loaded=new GameLibrary(root);check(loaded.entries.size()==1,"catalogue reload");check(loaded.entries.get(0).title.equals(e.title),"unicode title persists");check(loaded.entries.get(0).encoding().equals("UTF-8"),"content encoding persists");
   File catalogue=new File(root,"library.properties"), recovery=new File(root,"library.properties.backup");check(catalogue.renameTo(recovery),"simulate catalogue swap");check(new GameLibrary(root).entries.size()==1,"catalogue recovery");
   System.out.println("PASS: "+assertions+" archive/catalogue checks");
  }finally{SaveArchive.removeTree(root);}
 }
}
