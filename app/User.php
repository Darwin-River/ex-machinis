<?php

namespace App;

use Illuminate\Notifications\Notifiable;
use Illuminate\Foundation\Auth\User as Authenticatable;
use Illuminate\Support\Facades\Mail;

class User extends Authenticatable
{
    use Notifiable;

    /**
     * The attributes that are mass assignable.
     *
     * @var array
     */
    protected $fillable = [
        'name', 'email', 'password', 'credits'
    ];

    /**
     * The attributes that should be hidden for arrays.
     *
     * @var array
     */
    protected $hidden = [
        'password', 'remember_token',
    ];

    /**
     * Get the agents associated with the company.
     */
    public function agents()
    {
        return $this->hasManyThrough('App\Agent', 'App\Company');
    }

    /**
     * Registers user, creates company and initial drones, sends mail
     * @param $email string user's email
     * @param $name string user's name
     */
    public static function registerUser($email, $name)
    {
        $user = User::where('email', strtolower($email))->first();
        if ($user == null) {
            //create user
            //  echo 'creating new user<br/>';
            $user = User::create([
                'name' => $name,
                'email' => strtolower($email),
                'credits' => 1000,
            ]);
            $last_id = $user->id;
            $company = new Company();
            //$company->user_id = $user->id;
            //    $company->name = $data['company_name'];
            //$company->save();
            //    echo 'creating drones for user ' . $last_id . '<br/>';
            //create initial drones:
            $companyDrones = $company->generateStartingDrones($last_id);
            $data = [];
            foreach ($companyDrones as $i => $drone) {
                $data['drone' . ($i + 1)] = $drone->name;
            }
            /* echo*/
            Mail::send(['email.welcome_drones_html', 'email.welcome_drones_text'], $data, function ($message) use ($email, $name) {
                $message->to($email, $name)->from("registrar@" . getenv("MAIL_HOST"), "Ex Machinis")->subject('Welcome to Ex Machinis!');
            });
        } else {
            //notify user he's already registered
            // Get the user company ID and the drones currently assigned
            // $company = Company::where('user_id', $user->id)->first();

            //    echo 'User already registered: ' . $user->name . '<br/>';

            // Get now all the drones for this user and concatenate their email addresses
            $drones = Agent::where('user_id', $user->user_id)->get();
            $dronesInfo = "You're already registered with Ex Machinis and have access to the following drones:";

            foreach ($drones as $drone) {
                $dronesInfo .= "\n";
                $drone_email = $drone->name . "@" . getenv("MAIL_HOST");
                $dronesInfo .= $drone_email;
            }

            $dronesInfo .= "\n\n Please email help@exmachinis.com if you're having problems with the game.";

            $dronesInfo .= "\n\n You can learn more about the game at https://exmachinis.wiki.";

            Mail::raw($dronesInfo, function ($message) use ($email, $name) {
                $message->to($email, $name)
                    ->from("registrar@" . getenv("MAIL_HOST"), 'Ex Machinis Registrar')
                    ->subject("Already registered");
            });
        }
    }

}
