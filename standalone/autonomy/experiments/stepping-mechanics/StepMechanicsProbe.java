import com.google.gson.JsonParser;
import de.legoshi.parkourcalc.core.replay.StaticScene;
import de.legoshi.parkourcalc.core.sim.Vec3dCore;

public class StepMechanicsProbe {
    static void run(String name, String solids, double y, double dx, double dy, boolean ground) {
        StaticScene s = new StaticScene(new JsonParser().parse("{\"version\":1,\"profile\":\"minecraft-1.8.9\",\"bounds\":[-10,-5,-10,10,10,10],\"solids\":[" + solids + "]}").getAsJsonObject());
        StaticScene.Contact c = s.move(s.startBox(new Vec3dCore(.5,y,.5)),new Vec3dCore(dx,dy,0),ground,false);
        System.out.printf("%s: y=%.17g -> %.17g netY=%.17g rawY=%.17g x=%.17g stepped=%s grounded=%s supported=%s vertical=%s carryY=%.17g\n",name,y,c.position.y,c.position.y-y,dy,c.position.x,c.stepped,c.onGround,s.supported(c.box),c.verticalCollision,c.velocity.y);
    }
    public static void main(String[] args) {
        String wide = "[-5,0,-5,5,1,5],[1,1,-1,2,1.5,2]";
        String narrow = "[-1,0,-1,1,1,1],[1,1,-1,1.3,1.5,1]";
        run("walk",wide,1,.8,-.0784000015258789,true);
        run("jump_cancel",wide,1,.8,(double).42F,true);
        run("landing_blip",wide,1.2,.8,-.4,false);
        run("unsupported_blip",narrow,1.2,2,-.4,false);
        run("unsupported_cancel",narrow,1,2,(double).42F,true);
        run("return_leg_equality",wide,1.25,.8,-.3500000238418579,false);
    }
}
