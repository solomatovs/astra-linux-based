import java.util.ArrayList;
import java.util.List;

// Тест JDK: javac + jar + java + базовый рантайм.
// Коллекции и стримы поднимают java.base, MessageDigest — нативную часть (libjava/JNI),
// поэтому успешный запуск подтверждает всю цепочку целиком.
public class Hello {
    public static void main(String[] args) throws Exception {
        List<Integer> v = new ArrayList<>();
        for (int i = 1; i <= 10; i++) {
            v.add(i);
        }
        int sum = 0;
        for (int i : v) {
            sum += i;
        }

        java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
        byte[] digest = md.digest("hello".getBytes("UTF-8"));
        StringBuilder hex = new StringBuilder();
        for (int i = 0; i < 4; i++) {
            hex.append(String.format("%02x", digest[i]));
        }

        System.out.println("hello from java " + System.getProperty("java.version")
                + " (" + System.getProperty("java.vm.name") + ")");
        System.out.println("java.home = " + System.getProperty("java.home"));
        System.out.println("sum(1..10) = " + sum);
        System.out.println("sha256(hello)[0..3] = " + hex);

        if (sum != 55 || !hex.toString().equals("2cf24dba")) {
            System.exit(1);
        }
    }
}
